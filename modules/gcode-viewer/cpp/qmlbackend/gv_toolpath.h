// cpp/qmlbackend/gv_toolpath.h - контроллер вьювера GcodeToolpath как QML-тип.
//
// Этапы 5.4а и 5.4б плана переноса: C++-контроллер в URI MockBackend 1.0 вместо
// питоновского controller.py. Контракт QML (свойства, слоты, сигналы,
// objectName "gcodeToolpath") сохранён 1:1 - GcodeViewer3D.qml не правится.
//
// Что уже считается здесь: чтение УП, разбор и подготовка траектории (ядро
// gvcore: parseGcode + prepareOrdered), буферы rapid/feed (геометрии этапа 3,
// цель gvqtgeom), свойства center/radius/moveLength/minY/stages/loading/
// toolPosition, слоты sampleAt/isRapidAt/speedAt/setToolStage, асинхронная
// загрузка на QThreadPool с отменой устаревшего результата атомарным токеном.
//
// Модель STEP/STL (этап 5.4б): чтение модели (STEP - суп, рёбра B-Rep, нормали,
// поверхности за один проход через gvstep; STL - суп, ориентация оболочки,
// crease-aware нормали и обводка по двугранному углу), кожа partGeometry,
// обводка partEdges и hlrEdges (stride 48), силуэт hlrTangents на текущий глаз,
// partMeshCenter/partRadius и трансформа детали partRotation/partPosition из
// UI_ROTATION/UI_ORIGIN/UI_CAD_MATRIX/UI_STOCK. Заготовка публикуется
// (stockCenter/stockSize/hasStock) потому, что трансформа эталона подгоняет
// модель под её нижний угол: без заготовки положение детали разошлось бы.
//
// Инструмент, оправка и станок (этап 5.4в): профиль инструмента по UI_TOOL
// (toolGeometry, toolOutline, toolRings, toolLength, toolRadius), постоянная
// оправка ER20 (holderGeometry, holderOutline, holderRings, holderRadius),
// вылет оправки из UI_HOLDER (holderGauge), скан коллизий оправки о заготовку
// в фоне (collisions/collisionCount, слот analyzeCollisions) и сверка
// инструмента с таблицей стойки (toolCheck/toolIssueCount, слоты
// setMachineTools/setMachineLimits/runToolCheck). Ядро - gvcore: core/tooling и
// core/collision, те же числа и порядок операций, что у tooling.py и collision.py.
//
// Заглушек этого шага не осталось: отказ любой стадии виден в журнале
// (qCWarning плюс строка в stderr), молчаливого отказа нет.
//
// Сосуществование с питоновской регистрацией: QML-типы модуля MockBackend
// объявляет либо питоновский controller.py (@QmlElement), либо этот плагин.
// Оба одновременно в один движок не попадают: точки входа при включённом флаге
// CNC_CPP=controller не импортируют mock_backend и добавляют путь импорта
// плагина (см. gv_cpp.controller_enabled). Пустое значение флага - питоновский
// путь, как было.
//
// Потоки: разбор УП, обводка, модель и скан коллизий идут на пуле QThreadPool
// (объектов Qt ядро не создаёт); на GUI-поток результат возвращает сигнал
// toolpathReady или вызов очереди событий, а устаревший результат отбрасывает
// токен - int на GUI-потоке (скан коллизий, обводка) и std::atomic<int> у
// загрузки, чей результат читает рабочий поток.
#pragma once

#include <QObject>
#include <QQuaternion>
#include <QString>
#include <QVariant>
#include <QVariantList>
#include <QVariantMap>
#include <QVector3D>

#include <array>
#include <atomic>
#include <map>
#include <memory>
#include <optional>

#include "core/collision.h"
#include "core/path_prep.h"
#include "core/quality.h"
#include "core/silhouette.h"
#include "core/tangents.h"
#include "core/tooling.h"
#include "core/ui_tags.h"
#include "qmlbackend/gv_qmlgeom.h"

class QQuick3DGeometry;

namespace gvqml {

// Результат рабочего потока: токен загрузки, исходный текст УП, разрешённый путь
// модели рядом с программой и подготовленная траектория. Структура целиком в C++
// (ядро gvcore объектов Qt не создаёт), поэтому границу потоков переходит без
// блокировок.
struct PreparedPath {
    int token = 0;
    QString text;
    QString model_path;
    std::shared_ptr<const gv::PathData> data;
};

// Модель, прочитанная рабочим потоком: то, что питоновский _load_model_verts
// отдаёт про файл STEP или STL. Контейнеры лежат за shared_ptr: сигнал контракта
// несёт структуру копией (QVariant), и копия меша в десятки мегабайт была бы
// платой ни за что.
struct ModelData {
    bool is_step = false;                 // прочитан STEP (иначе STL)
    std::vector<double> verts;            // суп кожи (F*9 float64, плоский ряд)
    std::vector<float> normals;           // crease-aware нормали (F,3,3) float32
    bool has_step_edges = false;          // обводка B-Rep пришла вместе с моделью
    gv::PackedEdges step_edges;           // рёбра STEP после склейки и упаковки
    gv::StepSurfaceList surfaces;         // STEP: аналитические цилиндры и конусы
    gv::TangentFamilies families;         // STEP: семейства касательных
    gv::MeshTopology topo;                // топология силуэта меша
    // Уровень качества, с которым прочитана модель (CNC_QUALITY): обводка STL и
    // кадровый проход пересобираются с теми же допусками.
    gv::QualityPreset quality;
};

// Токен загрузки, путь модели и её данные: то, что несёт сигнал meshReady.
struct PreparedModel {
    int token = 0;
    QString path;
    std::shared_ptr<const ModelData> model;
};

class GcodeToolpath : public QObject
{
    Q_OBJECT
    QML_ELEMENT

    // ---- данные траектории ------------------------------------------------
    Q_PROPERTY(QVector3D center READ center NOTIFY pathChanged)
    Q_PROPERTY(double radius READ radius NOTIFY pathChanged)
    Q_PROPERTY(double moveLength READ moveLength NOTIFY pathChanged)
    Q_PROPERTY(double minY READ minY NOTIFY pathChanged)
    Q_PROPERTY(QVariantList stages READ stages NOTIFY pathChanged)
    Q_PROPERTY(QVariantList collisions READ collisions NOTIFY collisionsChanged)
    Q_PROPERTY(int collisionCount READ collisionCount NOTIFY collisionsChanged)
    Q_PROPERTY(QVariantList toolCheck READ toolCheck NOTIFY toolCheckChanged)
    Q_PROPERTY(int toolIssueCount READ toolIssueCount NOTIFY toolCheckChanged)
    Q_PROPERTY(QVector3D stockCenter READ stockCenter NOTIFY stockChanged)
    Q_PROPERTY(QVector3D stockSize READ stockSize NOTIFY stockChanged)
    Q_PROPERTY(bool hasStock READ hasStock NOTIFY stockChanged)
    Q_PROPERTY(double holderGauge READ holderGauge NOTIFY pathChanged)
    Q_PROPERTY(QVariantMap toolInfo READ toolInfo NOTIFY toolInfoChanged)
    Q_PROPERTY(double toolLength READ toolLength NOTIFY toolInfoChanged)
    Q_PROPERTY(double toolRadius READ toolRadius NOTIFY toolInfoChanged)
    Q_PROPERTY(double holderRadius READ holderRadius CONSTANT)
    Q_PROPERTY(QString modelPath READ modelPath NOTIFY modelPathChanged)
    Q_PROPERTY(QVector3D partMeshCenter READ partMeshCenter NOTIFY meshChanged)
    Q_PROPERTY(double partRadius READ partRadius NOTIFY meshChanged)
    Q_PROPERTY(QQuaternion partRotation READ partRotation NOTIFY partTransformChanged)
    Q_PROPERTY(QVector3D partPosition READ partPosition NOTIFY partTransformChanged)
    Q_PROPERTY(QVector3D toolPosition READ toolPosition NOTIFY toolChanged)
    Q_PROPERTY(double toolX READ toolX WRITE setToolX NOTIFY toolChanged)
    Q_PROPERTY(double toolY READ toolY WRITE setToolY NOTIFY toolChanged)
    Q_PROPERTY(double toolZ READ toolZ WRITE setToolZ NOTIFY toolChanged)
    Q_PROPERTY(bool loading READ loading NOTIFY loadingChanged)
    Q_PROPERTY(QVector3D eyeRaw READ eyeRaw NOTIFY eyeChanged)

    // ---- геометрии --------------------------------------------------------
    Q_PROPERTY(QQuick3DGeometry *rapidGeometry READ rapidGeometry NOTIFY ready)
    Q_PROPERTY(QQuick3DGeometry *feedGeometry READ feedGeometry NOTIFY ready)
    Q_PROPERTY(QQuick3DGeometry *partGeometry READ partGeometry NOTIFY meshChanged)
    Q_PROPERTY(QQuick3DGeometry *partEdges READ partEdges NOTIFY meshChanged)
    Q_PROPERTY(QQuick3DGeometry *hlrEdges READ hlrEdges NOTIFY meshChanged)
    Q_PROPERTY(QQuick3DGeometry *hlrTangents READ hlrTangents NOTIFY meshChanged)
    Q_PROPERTY(QQuick3DGeometry *toolGeometry READ toolGeometry NOTIFY toolInfoChanged)
    Q_PROPERTY(QQuick3DGeometry *toolOutline READ toolOutline NOTIFY toolInfoChanged)
    Q_PROPERTY(QQuick3DGeometry *holderGeometry READ holderGeometry CONSTANT)
    Q_PROPERTY(QQuick3DGeometry *holderOutline READ holderOutline CONSTANT)
    Q_PROPERTY(QQuick3DGeometry *toolRings READ toolRings NOTIFY toolInfoChanged)
    Q_PROPERTY(QQuick3DGeometry *holderRings READ holderRings CONSTANT)

public:
    explicit GcodeToolpath(QObject *parent = nullptr);

    // ---- свойства: данные траектории --------------------------------------
    QVector3D center() const;
    double radius() const { return radius_; }
    double moveLength() const { return move_length_; }
    double minY() const { return min_y_; }
    QVariantList stages() const { return stages_; }
    QVariantList collisions() const { return collisions_; }
    int collisionCount() const { return int(collisions_.size()); }
    QVariantList toolCheck() const { return tool_check_; }
    int toolIssueCount() const;
    QVector3D stockCenter() const { return stock_center_; }
    QVector3D stockSize() const { return stock_size_; }
    bool hasStock() const { return has_stock_; }
    double holderGauge() const { return holder_gauge_; }
    QVariantMap toolInfo() const { return tool_info_; }
    double toolLength() const { return tool_length_; }
    double toolRadius() const { return tool_radius_; }
    double holderRadius() const { return holder_radius_; }
    QString modelPath() const { return model_path_; }
    QVector3D partMeshCenter() const { return mesh_center_; }
    double partRadius() const;
    QQuaternion partRotation() const { return part_rotation_; }
    QVector3D partPosition() const { return part_position_; }
    QVector3D toolPosition() const;
    double toolX() const { return tool_[0]; }
    double toolY() const { return tool_[1]; }
    double toolZ() const { return tool_[2]; }
    void setToolX(double v);
    void setToolY(double v);
    void setToolZ(double v);
    bool loading() const { return loading_; }
    QVector3D eyeRaw() const { return eye_raw_; }

    // ---- свойства: геометрии ---------------------------------------------
    QQuick3DGeometry *rapidGeometry() const;
    QQuick3DGeometry *feedGeometry() const;
    QQuick3DGeometry *partGeometry() const;
    QQuick3DGeometry *partEdges() const;
    QQuick3DGeometry *hlrEdges() const;
    QQuick3DGeometry *hlrTangents() const;
    QQuick3DGeometry *toolGeometry() const;
    QQuick3DGeometry *toolOutline() const;
    QQuick3DGeometry *holderGeometry() const;
    QQuick3DGeometry *holderOutline() const;
    QQuick3DGeometry *toolRings() const;
    QQuick3DGeometry *holderRings() const;

    // ---- слоты QML --------------------------------------------------------
    // Показ инструмента этапа index: метаданные UI_TOOL этапа публикуются
    // (профиль инструмента - следующая часть шага).
    Q_INVOKABLE void setToolStage(int index);
    Q_INVOKABLE void analyzeCollisions();
    Q_INVOKABLE void setMachineTools(const QVariantList &tools);
    Q_INVOKABLE void setMachineLimits(const QVariantMap &limits);
    Q_INVOKABLE void runToolCheck();
    Q_INVOKABLE void refreshTangents(const QVector3D &view_point);
    // Синхронная загрузка УП (тесты и headless): возвращает число сегментов.
    Q_INVOKABLE int load_gcode(const QString &filepath);
    // Загрузка только модели (STEP/STL): читает файл и публикует кожу с обводкой.
    Q_INVOKABLE bool loadModel(const QString &filepath);
    // Асинхронная загрузка: разбор и буферы на QThreadPool.
    Q_INVOKABLE int load_gcode_async(const QString &filepath);
    // Загрузка УП из строки: файла рядом нет, поэтому и модели тоже.
    Q_INVOKABLE int load_string(const QString &text);
    Q_INVOKABLE QVector3D sampleAt(double d) const;
    Q_INVOKABLE bool isRapidAt(double d) const;
    Q_INVOKABLE double speedAt(double d) const;

signals:
    void ready();
    void pathChanged();
    void modelPathChanged();
    void meshChanged();
    void partTransformChanged();
    void stockChanged();
    void toolChanged();
    void loadingChanged();
    void toolInfoChanged();
    void collisionsChanged();
    void toolCheckChanged();
    void loadFinished();
    void eyeChanged();
    void toolpathLoaded(int segment_count);
    void toolpathReady(const QVariant &payload);
    void edgesReady(const QVariant &payload);
    void meshReady(const QVariant &payload);

private:
    // GUI-поток: применить траекторию, подготовленную рабочим потоком.
    void onToolpathReady(const QVariant &payload);
    void applyToolpathData(const std::shared_ptr<const gv::PathData> &data, const QString &text);
    void applyToolInfo(const gv::ToolInfo &spec);
    void setLoading(bool v);
    void setModelPath(const QString &path);
    void setTool(double x, double y, double z);
    // Пустая обводка: свежий объект, иначе QML не перезаливает буфер.
    void clearEdges();
    // Индекс сегмента, содержащего путь d (двоичный поиск по накопленной длине).
    int indexAt(double d) const;
    // Предупреждение о заглушке - один раз за прогон на ключ.
    void warnDeferred(const char *key, const QString &text);

    // ---- модель, обводка и силуэт (этап 5.4б) -----------------------------
    // Теги сцены из текста УП: UI_ROTATION, UI_ORIGIN, UI_CAD_MATRIX,
    // UI_CAD_MATRIX_ORIGIN и UI_STOCK. Из них собирается трансформа детали и
    // место заготовки в сцене.
    void applySceneTags(const QString &text);
    // Итоговая трансформа детали: сцена <- база <- WCS <- CAD (питоновский
    // _rebuild_part_transform).
    void rebuildPartTransform();
    // GUI-поток: применить модель, прочитанную рабочим потоком (или nullptr -
    // модель не прочитана: кожа и обводка пустеют, как у эталона).
    void applyModelData(const std::shared_ptr<const ModelData> &model);
    void onMeshReady(const QVariant &payload);
    // Синхронный путь: прочитать модель по пути (пустой путь - модели нет) и
    // опубликовать кожу с обводкой. Нужен load_gcode и loadModel.
    void applyModelPath(const QString &path);
    // Обводка вторым проходом, как у эталона: STEP - готовая из чтения модели,
    // STL - рёбра по двугранному углу (до 0.4 с на модель, поэтому не на
    // GUI-потоке). Результат применяется по токену: устаревшая обводка уходит.
    void startEdgesTask(const std::shared_ptr<const ModelData> &model, int token);
    void setEdgesPacked(const gv::PackedEdges &packed);
    // Общий центр габарита кожи для обеих лент контура (радиальный отжим).
    void syncHlrReference();
    // Публикация силуэта: сегменты (M,2,3) float32, пустой вектор - убрать.
    void setSilhouetteSegments(const std::vector<float> &segments);

    // ---- инструмент, оправка и коллизии (этап 5.4в) ------------------------
    // Тело вращения по профилю инструмента (пустой профиль - пустая геометрия).
    static void setToolProfile(ToolGeometry *geo, const std::vector<gv::ProfilePoint> &profile);
    // Кольца профиля в Lines-буфер: тот же _pack_edge_segments, что у эталона.
    static void setRingSegments(ToolpathGeometry *geo,
                                const std::vector<gv::RingSegment> &segments);
    // Отчёт сверки строкой контракта QML (None у эталона - недействительный QVariant).
    static QVariantMap toolCheckMap(const gv::ToolCheckRow &row);
    // Строка коллизии картой контракта QML: d, depth, scene, scene_hb.
    static QVariantMap collisionMap(const gv::CollisionRange &range);

    std::shared_ptr<const gv::PathData> data_;

    ToolpathGeometry *rapid_ = nullptr;
    ToolpathGeometry *feed_ = nullptr;
    MeshGeometry *mesh_ = nullptr;
    ToolpathGeometry *edges_ = nullptr;
    HLRLineGeometry *hlr_edges_ = nullptr;
    HLRLineGeometry *hlr_tangents_ = nullptr;
    ToolGeometry *tool_geo_ = nullptr;
    ToolGeometry *tool_outline_geo_ = nullptr;
    ToolGeometry *holder_geo_ = nullptr;
    ToolGeometry *holder_outline_geo_ = nullptr;
    ToolpathGeometry *tool_rings_geo_ = nullptr;
    ToolpathGeometry *holder_rings_geo_ = nullptr;

    QVariantList stages_;                 // [{start, end, label, tool}]
    std::vector<gv::ToolInfo> stage_specs_;  // метаданные UI_TOOL по этапам
    QVariantList collisions_;             // коллизии оправки с заготовкой
    QVariantList tool_check_;             // отчёт сверки инструмента
    std::map<int, gv::MachineTool> machine_tools_;  // таблица стойки, мм
    gv::MachineLimits machine_limits_;    // пределы осей стойки
    gv::ToolInfo tool_spec_;              // текущий инструмент (профиль и геометрии)
    QVariantMap tool_info_;               // разобранный UI_TOOL для QML
    QString model_path_;                  // сайдкар модели рядом с УП

    // ---- состояние модели, обводки и трансформы ---------------------------
    bool has_step_edges_ = false;         // обводка B-Rep пришла с моделью
    gv::PackedEdges step_edges_;          // рёбра STEP: склейка и упаковка
    gv::StepSurfaceList rev_surfaces_;    // STEP: аналитические цилиндры и конусы
    bool has_tan_fams_ = false;           // семейства касательных посчитаны
    gv::TangentFamilies tan_fams_;        // STEP: семейства касательных
    gv::MeshTopology mesh_topo_;          // топология силуэта меша
    bool has_topo_ = false;               // меш есть - силуэт считается по нему
    // Буферы кадрового прохода: ёмкости переживают кадр (у Test_model 21
    // выделение на кадр в режиме меша и 31 в аналитическом).
    gv::MeshSilhouetteScratch sil_scratch_;
    gv::TangentScratch tan_scratch_;
    std::vector<float> tan_segments_;     // касательные float32 для буфера лент
    bool has_mesh_ = false;               // габарит кожи известен
    // Уровень качества прочитанной модели (CNC_QUALITY): обводка STL и кадровый
    // проход идут с теми же допусками, что и чтение. По умолчанию - med.
    gv::QualityPreset quality_;
    std::array<double, 3> mesh_lo_{0.0, 0.0, 0.0};
    std::array<double, 3> mesh_hi_{0.0, 0.0, 0.0};
    int edge_token_ = 0;                  // отменяет устаревшую обводку (GUI-поток)
    std::array<double, 3> rot_{0.0, 0.0, 0.0};        // UI_ROTATION, градусы
    std::array<double, 3> model_pos_{0.0, 0.0, 0.0};  // UI_ORIGIN, координаты сцены
    bool has_cad_ = false;                // UI_CAD_MATRIX найден
    std::array<double, 9> cad_matrix_{};  // столбцы X, Y, Z матрицы CAD
    std::array<double, 3> cad_origin_{0.0, 0.0, 0.0}; // UI_CAD_MATRIX_ORIGIN
    std::array<double, 3> stock_lower_m_{0.0, 0.0, 0.0};  // нижний угол, машинные
    std::array<double, 3> part_offset_{0.0, 0.0, 0.0};    // поправка CNC_OFS_M*
    // Габарит заготовки в машинных координатах: по нему идёт скан коллизий.
    std::optional<std::array<double, 6>> stock_bounds_;
    // UI_HOLDER: вылет оправки (он же holderGauge) и имя.
    std::optional<gv::HolderTag> holder_tag_;
    int collision_token_ = 0;             // отменяет устаревший скан (GUI-поток)

    // Токены: устаревший результат рабочего потока отбрасывается. Атомарный
    // счётчик взят по указателю: рабочий поток читает счётчик, а не контроллер
    // (объект может исчезнуть, пока загрузка идёт), а GUI-поток только пишет.
    std::shared_ptr<std::atomic<int>> load_token_ = std::make_shared<std::atomic<int>>(0);

    double radius_ = 50.0;
    double move_length_ = 0.0;
    double min_y_ = 0.0;
    double tool_length_ = 0.0;
    double tool_radius_ = 0.0;
    double holder_radius_ = 0.0;
    double holder_gauge_ = 0.0;
    double tool_[3] = {0.0, 0.0, 0.0};
    bool loading_ = false;
    bool has_stock_ = false;
    QVector3D mesh_center_;
    QVector3D stock_center_;
    QVector3D stock_size_;
    QVector3D eye_raw_;
    QQuaternion part_rotation_;
    QVector3D part_position_;
};

} // namespace gvqml

Q_DECLARE_METATYPE(gvqml::PreparedPath)
Q_DECLARE_METATYPE(gvqml::PreparedModel)
