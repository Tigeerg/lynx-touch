// cpp/qmlbackend/gv_toolpath.h - the viewer controller GcodeToolpath as a QML type.
//
// Stages 5.4a and 5.4b of the port: a C++ controller under the URI MockBackend 1.0
// instead of the reference qmlbackend/gv_toolpath.cpp. The QML contract
// (properties, slots, signals, objectName "gcodeToolpath") is kept 1:1 -
// GcodeViewer3D.qml is not edited.
//
// What is already computed here: NC reading, parsing and path preparation (the
// gvcore core: parseGcode + prepareOrdered), the rapid/feed buffers (stage 3
// geometries, target gvqtgeom), the properties center/radius/moveLength/minY/
// stages/loading/toolPosition, the slots sampleAt/isRapidAt/speedAt/setToolStage,
// and asynchronous loading on a QThreadPool with cancellation of a stale result by
// an atomic token.
//
// The STEP/STL model (stage 5.4b): model reading (STEP - soup, B-Rep edges,
// normals, surfaces in one pass through gvstep; STL - soup, shell orientation,
// crease-aware normals and dihedral-angle outline), the skin partGeometry, the
// outline partEdges and hlrEdges (stride 48), the silhouette hlrTangents at the
// current eye, partMeshCenter/partRadius and the part transform
// partRotation/partPosition from UI_ROTATION/UI_ORIGIN/UI_CAD_MATRIX/UI_STOCK.
// The stock is published (stockCenter/stockSize/hasStock) because the reference
// transform fits the model to its lower corner: without the stock the part
// position would diverge.
//
// Tool, holder and machine (stage 5.4c): the tool profile from UI_TOOL
// (toolGeometry, toolOutline, toolRings, toolLength, toolRadius), the constant
// ER20 holder (holderGeometry, holderOutline, holderRings, holderRadius), the
// holder gauge length from UI_HOLDER (holderGauge), the background scan of holder
// collisions with the stock (collisions/collisionCount, slot analyzeCollisions)
// and the tool check against the machine table (toolCheck/toolIssueCount, slots
// setMachineTools/setMachineLimits/runToolCheck). The core is gvcore: core/tooling
// and core/collision, the same numbers and order of operations as
// core/tooling.cpp and core/collision.cpp.
//
// No stubs are left from this step: the failure of any stage is visible in the
// log (qCWarning plus a line in stderr), there is no silent failure.
//
// Coexistence with the reference registration: the QML types of the MockBackend
// module are declared either by the reference qmlbackend/gv_toolpath.cpp
// (@QmlElement) or by this plugin. Both never reach one engine at once: the entry
// points, with the flag CNC_CPP=controller set, do not import mock_backend and add
// the plugin import path (see gv_cpp.controller_enabled). An empty flag value is
// the reference path, as before.
//
// Threads: NC parsing, outline, model and collision scan run on a QThreadPool (the
// core creates no Qt objects); the result returns to the GUI thread by the
// toolpathReady signal or an event-queue call, and a stale result is discarded by
// a token - an int on the GUI thread (collision scan, outline) and a
// std::atomic<int> for loading, whose result is read by a worker thread.
#pragma once

#include <QByteArray>
#include <QObject>
#include <QQuick3DTextureData>
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
#include "core/stock_sim.h"
#include "core/tangents.h"
#include "core/tooling.h"
#include "core/ui_tags.h"
#include "qmlbackend/gv_qmlgeom.h"

class QQuick3DGeometry;

namespace gvqml {

// Result of a worker thread: the load token, the source NC text, the resolved
// model path next to the program and the prepared path. The structure is entirely
// C++ (the gvcore core creates no Qt objects), so it crosses the thread boundary
// without locks.
struct PreparedPath {
    int token = 0;
    QString text;
    QString model_path;
    std::shared_ptr<const gv::PathData> data;
};

// The model read by a worker thread: what the reference _load_model_verts returns
// about a STEP or STL file. The containers live behind shared_ptr: the contract
// signal carries the structure by copy (QVariant), and copying a mesh of tens of
// megabytes would be a price paid for nothing.
struct ModelData {
    bool is_step = false;                 // STEP was read (otherwise STL)
    std::vector<double> verts;            // skin soup (F*9 float64, flat row)
    std::vector<float> normals;           // crease-aware normals (F,3,3) float32
    bool has_step_edges = false;          // the B-Rep outline came with the model
    gv::PackedEdges step_edges;           // STEP edges after welding and packing
    gv::StepSurfaceList surfaces;         // STEP: analytic cylinders and cones
    gv::TangentFamilies families;         // STEP: tangent families
    gv::MeshTopology topo;                // mesh silhouette topology
    // The quality level the model was read with (CNC_QUALITY): the STL outline and
    // the per-frame pass are rebuilt with the same tolerances.
    gv::QualityPreset quality;
};

// The load token, the model path and its data: what the meshReady signal carries.
struct PreparedModel {
    int token = 0;
    QString path;
    std::shared_ptr<const ModelData> model;
};

// Removal token: the generation number and the cancel time. The generation is read
// by a worker thread (the object may disappear while removal runs), so the counter
// lives in a separate structure behind shared_ptr. `cancel_us` is the monotonic
// time of the cancel request: the worker thread subtracts it from its stop time and
// gets the latency.
struct StockRunToken {
    std::atomic<int> generation{0};
    std::atomic<long long> cancel_us{-1};
};

// The live walk together with the path: the path is held by shared_ptr while the
// walk is alive - the controller may change the program while a worker thread is
// still reading the previous one. The field and the walk cursor are touched only by
// the worker thread (one tick at a time); the GUI thread reads only the path from
// the object.
struct StockSession {
    std::shared_ptr<const gv::PathData> data;
    gv::StockWalk walk;
};

// A ready field from a worker thread. The bytes are copied on the worker thread
// (the whole buffer; QQuick3DTextureData has no partial upload), so the GUI thread
// only swaps the published field. `session` is filled on a full pass: it is a walk
// with the cursor at the program end, from which live ticks continue.
struct StockPassPayload {
    QByteArray depth;                  // R32F, count_x * count_y float32
    QVariantList bounds;               // [xmin, xmax, ymin, ymax, zmin, zmax], mm
    double cell_mm = 0.5;
    int count_x = 0;
    int count_y = 0;
    double target_mm = 0.0;            // path length up to which the field is removed
    double total_mm = 0.0;
    double time_ms = 0.0;              // the walk; on a full pass also the field reset
    bool has_field = false;
    bool cancelled = false;            // the tick was interrupted by the token: not published
    bool refused = false;              // the tool is not expressible by the map
    bool failed = false;               // an exception in the core: not published
    int refused_stage = -1;
    int refused_tool = 0;
    std::shared_ptr<StockSession> session;   // on a full pass: the live walk
};

class GcodeToolpath : public QObject
{
    Q_OBJECT
    QML_ELEMENT

    // ---- path data --------------------------------------------------------
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
    // Material removal simulation (core/stock_sim): the height field as is, in
    // machine coordinates. stockDepth - R32F (float32, a row per Y), stockBounds -
    // [xmin, xmax, ymin, ymax, zmin, zmax] in mm, stockSimState - idle, computing,
    // ready, cancelled or refused (the tool is not expressible by the map; no
    // field), stockSimProgress - the fraction of the path length up to which the
    // field is computed. stockHasField - the field is published in full and can be
    // drawn; while a new one is computed, the previous whole field is published (it
    // also remains after a cancel), so drawing looks at this flag, not at the
    // state. stockSimTimeMs - the cost of a full pass, stockSimTickMs - the cost of
    // the last live tick (walk plus field copy), ms. stockField - the same field as
    // an R32F texture for drawing (step 3): a buffer row is a texture row, a node
    // is a texel; it lives from controller creation, so the Texture in QML is never
    // left without data and the engine does not complain.
    Q_PROPERTY(QByteArray stockDepth READ stockDepth NOTIFY stockSimChanged)
    Q_PROPERTY(QVariantList stockBounds READ stockBounds NOTIFY stockSimChanged)
    Q_PROPERTY(double stockCellMm READ stockCellMm NOTIFY stockSimChanged)
    Q_PROPERTY(int stockCountX READ stockCountX NOTIFY stockSimChanged)
    Q_PROPERTY(int stockCountY READ stockCountY NOTIFY stockSimChanged)
    Q_PROPERTY(bool stockHasField READ stockHasField NOTIFY stockSimChanged)
    Q_PROPERTY(QQuick3DTextureData *stockField READ stockField NOTIFY stockSimChanged)
    Q_PROPERTY(QString stockSimState READ stockSimState NOTIFY stockSimStateChanged)
    Q_PROPERTY(double stockSimProgress READ stockSimProgress NOTIFY stockSimProgressChanged)
    Q_PROPERTY(double stockSimTimeMs READ stockSimTimeMs NOTIFY stockSimChanged)
    Q_PROPERTY(double stockSimTickMs READ stockSimTickMs NOTIFY stockSimChanged)
    Q_PROPERTY(double stockSimCancelMs READ stockSimCancelMs NOTIFY stockSimChanged)
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

    // ---- geometries -------------------------------------------------------
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
    Q_PROPERTY(QQuick3DGeometry *toolProfileEdges READ toolProfileEdges NOTIFY toolInfoChanged)
    Q_PROPERTY(QQuick3DGeometry *holderProfileEdges READ holderProfileEdges CONSTANT)

public:
    explicit GcodeToolpath(QObject *parent = nullptr);
    // Waits for the pool task: they call the controller by this (invokeMethod,
    // reportRefusal), and a task living longer than the object hits a dead QObject
    // - SIGSEGV in QObject::thread on exit (Test_model.ngc, code 139).
    ~GcodeToolpath() override;

    // ---- properties: path data --------------------------------------------
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
    QByteArray stockDepth() const { return stock_depth_; }
    QVariantList stockBounds() const { return stock_bounds_list_; }
    QQuick3DTextureData *stockField() const { return stock_field_; }
    double stockCellMm() const { return stock_cell_mm_; }
    int stockCountX() const { return stock_count_x_; }
    int stockCountY() const { return stock_count_y_; }
    bool stockHasField() const { return stock_has_field_; }
    QString stockSimState() const { return stock_sim_state_; }
    double stockSimProgress() const { return stock_sim_progress_; }
    double stockSimTimeMs() const { return stock_sim_time_ms_; }
    double stockSimTickMs() const { return stock_sim_tick_ms_; }
    double stockSimCancelMs() const { return stock_sim_cancel_ms_; }
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

    // ---- properties: geometries -------------------------------------------
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
    QQuick3DGeometry *toolProfileEdges() const;
    QQuick3DGeometry *holderProfileEdges() const;

    // ---- QML slots --------------------------------------------------------
    // Show the tool of stage index: the stage's UI_TOOL metadata is published (the
    // tool profile is the next part of the step).
    Q_INVOKABLE void setToolStage(int index);
    Q_INVOKABLE void analyzeCollisions();
    // Cancel removal without replacement: the state becomes cancelled, no new field
    // is published. The last whole field stays on screen, the live walk is dropped:
    // a cancel stops the work, it does not rewrite what is already computed. A
    // program change cancels the old removal itself, along the same path.
    Q_INVOKABLE void cancelStockSim();
    // Live publication: the field must reflect removal up to pathDistMm. Called
    // from QML about 10 times per second while playback runs (and once on a stop or
    // seek). The tick runs in the pool and continues the walk from the last whole
    // move; backwards along the path the walk starts over. The GUI does not wait.
    Q_INVOKABLE void stockFollow(double pathDistMm);
    Q_INVOKABLE void setMachineTools(const QVariantList &tools);
    Q_INVOKABLE void setMachineLimits(const QVariantMap &limits);
    Q_INVOKABLE void runToolCheck();
    Q_INVOKABLE void refreshTangents(const QVector3D &view_point);
    // The tool tip position in the scene frame: the silhouette eye of a surface of
    // revolution is measured from it. QML calls it before refreshTangents on every
    // frame.
    Q_INVOKABLE void setToolTip(const QVector3D &tip);
    // Synchronous NC load (tests and headless): returns the segment count.
    Q_INVOKABLE int load_gcode(const QString &filepath);
    // Load only the model (STEP/STL): reads the file and publishes the skin with
    // the outline.
    Q_INVOKABLE bool loadModel(const QString &filepath);
    // Asynchronous load: parsing and buffers on a QThreadPool.
    Q_INVOKABLE int load_gcode_async(const QString &filepath);
    // Load the NC from a string: there is no file nearby, so no model either.
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
    void stockSimChanged();
    void stockSimStateChanged();
    void stockSimProgressChanged();
    void toolCheckChanged();
    void loadFinished();
    void eyeChanged();
    void toolpathLoaded(int segment_count);
    void toolpathReady(const QVariant &payload);
    void edgesReady(const QVariant &payload);
    void meshReady(const QVariant &payload);

private:
    // GUI thread: apply the path prepared by a worker thread.
    void onToolpathReady(const QVariant &payload);
    void applyToolpathData(const std::shared_ptr<const gv::PathData> &data, const QString &text);
    void applyToolInfo(const gv::ToolInfo &spec);
    void setLoading(bool v);
    void setModelPath(const QString &path);
    void setTool(double x, double y, double z);
    // Empty outline: a fresh object, otherwise QML does not re-upload the buffer.
    void clearEdges();
    // Index of the segment containing path d (binary search over the accumulated
    // length).
    int indexAt(double d) const;
    // Stub warning - once per run per key.
    void warnDeferred(const char *key, const QString &text);

    // ---- model, outline and silhouette (stage 5.4b) -----------------------
    // Scene tags from the NC text: UI_ROTATION, UI_ORIGIN, UI_CAD_MATRIX,
    // UI_CAD_MATRIX_ORIGIN and UI_STOCK. The part transform and the stock position
    // in the scene are assembled from them.
    void applySceneTags(const QString &text);
    // The final part transform: scene <- base <- WCS <- CAD (the reference
    // _rebuild_part_transform).
    void rebuildPartTransform();
    // GUI thread: apply the model read by a worker thread (or nullptr - the model
    // was not read: the skin and the outline become empty, as in the reference).
    void applyModelData(const std::shared_ptr<const ModelData> &model);
    void onMeshReady(const QVariant &payload);
    // Synchronous path: read the model by path (an empty path - no model) and
    // publish the skin with the outline. Needed by load_gcode and loadModel.
    void applyModelPath(const QString &path);
    // Outline in a second pass, as in the reference: STEP - ready from the model
    // read, STL - edges by dihedral angle (up to 0.4 s per model, so not on the GUI
    // thread). The result is applied by token: a stale outline is dropped.
    void startEdgesTask(const std::shared_ptr<const ModelData> &model, int token);
    void setEdgesPacked(const gv::PackedEdges &packed);
    // Common bounds centre of the skin for both contour ribbons (radial offset).
    void syncHlrReference();
    // Publish the silhouette: segments (M,2,3) float32, an empty vector - remove.
    void setSilhouetteSegments(const std::vector<float> &segments);

    // ---- tool, holder and collisions (stage 5.4c) -------------------------
    // Surface of revolution from a tool profile (an empty profile - empty geometry).
    static void setToolProfile(ToolGeometry *geo, const std::vector<gv::ProfilePoint> &profile);
    // Profile rings into a Lines buffer: the same _pack_edge_segments as the
    // reference.
    static void setRingSegments(ToolpathGeometry *geo,
                                const std::vector<gv::RingSegment> &segments);
    // Edges of a surface of revolution as stepledges ribbons: the rings (the same
    // sampling as toolRings) plus the profile's tangent generators at the camera eye
    // - via the same tangentFromFamilies as the part silhouette.
    static void setProfileEdges(HLRLineGeometry *geo,
                                const std::vector<gv::ProfilePoint> &profile,
                                const gv::TangentFamilies &families,
                                const double eye_mm[3],
                                gv::TangentScratch &scratch);
    // Rebuild the tool and holder edges at the saved camera eye.
    void refreshProfileEdges();
    // The check report as a QML contract row (the reference's None - an invalid
    // QVariant).
    static QVariantMap toolCheckMap(const gv::ToolCheckRow &row);
    // A collision row as a QML contract map: d, depth, scene, scene_hb.
    static QVariantMap collisionMap(const gv::CollisionRange &range);

    // ---- removal simulation (core/stock_sim) ------------------------------
    // Start a full pass over data_ in the background: the field to the end of the
    // program. The old removal is cancelled by token: its field is not published.
    // Without a path or UI_STOCK bounds there is nothing to publish.
    void startStockSim();
    // One live tick: continue the walk to stock_live_target_ in the pool. While the
    // tick is computed, the previous whole field is on screen.
    void startStockTick();
    // Publish a ready field: depth R32F, bounds, step, nodes and time. The field is
    // swapped only here - integrity matters more than freshness.
    void applyStockPass(const StockPassPayload &payload);
    // The field is unpublished: bytes, bounds, nodes and texture (1x1 again).
    void clearStockField();

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
    HLRLineGeometry *tool_profile_edges_ = nullptr;
    HLRLineGeometry *holder_profile_edges_ = nullptr;
    // Profiles and their tangent families: the families depend on the profile, not
    // on the eye, so they are built when the tool changes, and a frame only runs
    // the per-frame pass tangentFromFamilies.
    std::vector<gv::ProfilePoint> tool_profile_;
    std::vector<gv::ProfilePoint> holder_profile_;
    gv::TangentFamilies tool_tan_fams_;
    gv::TangentFamilies holder_tan_fams_;
    gv::TangentScratch tool_profile_scratch_;
    gv::TangentScratch holder_profile_scratch_;
    QVector3D view_point_;                // the last camera eye in the scene frame
    bool has_view_point_ = false;
    QVector3D tool_tip_;                  // tool tip in the scene frame

    QVariantList stages_;                 // [{start, end, label, tool}]
    std::vector<gv::ToolInfo> stage_specs_;  // UI_TOOL metadata per stage
    QVariantList collisions_;             // holder collisions with the stock
    QVariantList tool_check_;             // tool check report
    std::map<int, gv::MachineTool> machine_tools_;  // machine table, mm
    gv::MachineLimits machine_limits_;    // machine axis limits
    gv::ToolInfo tool_spec_;              // current tool (profile and geometries)
    gv::ToolInfo program_tool_;           // program tool before the first UI_TOOL stage
    QVariantMap tool_info_;               // parsed UI_TOOL for QML
    QString model_path_;                  // model sidecar next to the NC program

    // ---- model, outline and transform state -------------------------------
    bool has_step_edges_ = false;         // the B-Rep outline came with the model
    gv::PackedEdges step_edges_;          // STEP edges: welding and packing
    gv::StepSurfaceList rev_surfaces_;    // STEP: analytic cylinders and cones
    bool has_tan_fams_ = false;           // tangent families are computed
    gv::TangentFamilies tan_fams_;        // STEP: tangent families
    gv::MeshTopology mesh_topo_;          // mesh silhouette topology
    bool has_topo_ = false;               // there is a mesh - the silhouette is computed from it
    // Per-frame pass buffers: the capacities survive the frame (on Test_model 21
    // allocations per frame in mesh mode and 31 in analytic mode).
    gv::MeshSilhouetteScratch sil_scratch_;
    gv::TangentScratch tan_scratch_;
    std::vector<float> tan_segments_;     // tangent float32 for the ribbon buffer
    bool has_mesh_ = false;               // the skin bounds are known
    // The quality level of the read model (CNC_QUALITY): the STL outline and the
    // per-frame pass use the same tolerances as the read. Default - med.
    gv::QualityPreset quality_;
    std::array<double, 3> mesh_lo_{0.0, 0.0, 0.0};
    std::array<double, 3> mesh_hi_{0.0, 0.0, 0.0};
    int edge_token_ = 0;                  // cancels a stale outline (GUI thread)
    std::array<double, 3> rot_{0.0, 0.0, 0.0};        // UI_ROTATION, degrees
    std::array<double, 3> model_pos_{0.0, 0.0, 0.0};  // UI_ORIGIN, scene coordinates
    bool has_cad_ = false;                // UI_CAD_MATRIX found
    std::array<double, 9> cad_matrix_{};  // columns X, Y, Z of the CAD matrix
    std::array<double, 3> cad_origin_{0.0, 0.0, 0.0}; // UI_CAD_MATRIX_ORIGIN
    std::array<double, 3> stock_lower_m_{0.0, 0.0, 0.0};  // lower corner, machine
    std::array<double, 3> part_offset_{0.0, 0.0, 0.0};    // CNC_OFS_M* correction
    // Stock bounds in machine coordinates: the collision scan uses them.
    std::optional<std::array<double, 6>> stock_bounds_;
    // UI_HOLDER: the holder gauge length (also holderGauge) and name.
    std::optional<gv::HolderTag> holder_tag_;
    int collision_token_ = 0;             // cancels a stale scan (GUI thread)

    // Removal simulation: the field is already computed but not yet drawn (step 3).
    QByteArray stock_depth_;              // float32 R32F, countX * countY
    QVariantList stock_bounds_list_;      // [xmin, xmax, ymin, ymax, zmin, zmax]
    // The same field as a texture for the vertex shader: R32F, a buffer row is a
    // texture row. The object is created once (1x1): the field is published by
    // updating the data, not by creating a texture.
    QQuick3DTextureData *stock_field_ = nullptr;
    double stock_cell_mm_ = 0.5;
    int stock_count_x_ = 0;
    int stock_count_y_ = 0;
    bool stock_has_field_ = false;
    QString stock_sim_state_ = QStringLiteral("idle");
    double stock_sim_progress_ = 0.0;
    double stock_sim_time_ms_ = 0.0;
    double stock_sim_tick_ms_ = 0.0;
    double stock_sim_cancel_ms_ = -1.0;   // -1: the cancel has not been measured yet
    int stock_cancel_reported_ = 0;       // generation of the last measured cancel
    std::shared_ptr<StockRunToken> stock_token_ = std::make_shared<StockRunToken>();
    // Live walk: the field and the cursor the tick continues from. Empty - no live
    // removal (a full pass is still computed, a refusal or a cancel).
    std::shared_ptr<StockSession> stock_session_;
    // Target of the last tick, mm of path (GUI thread). There is one tick in the
    // pool: a call during a tick only moves the target, and the tick completion
    // catches up with it.
    double stock_live_target_ = 0.0;
    bool stock_tick_in_flight_ = false;

    // Tokens: a stale worker-thread result is discarded. The atomic counter is
    // taken by pointer: the worker thread reads the counter, not the controller
    // (the object may disappear while loading runs), and the GUI thread only writes.
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
