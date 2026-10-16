// Screen-space constant-width ribbons for CAD outline lines (CustomMaterial
// vertex stage, chunk API like toolpath.vert).
//
// Geometry supplies POSITION (line endpoint, floated off the skin by the
// CPU-side FLOAT_SCALE), NORMAL (segment unit direction) and UV0.x (ribbon
// side, -1/+1). The segment is projected to clip space, the screen-space
// perpendicular comes from the two clip endpoints, and the vertex is
// offset by half the requested pixel width scaled by clip.w - exact after
// the perspective divide. RHI renders true GL_LINES at 1 px and MSAA
// coverage thins those to a hatch that disappears on zoom-in; ribbons keep
// a crisp constant weight at any zoom. The rasterizer depth test against
// the opaque skin hides what the body covers - real CAD visibility, live
// while orbiting.
//
// lineWidthPx, viewportW, viewportH, eyeRaw and camFloatMm are uniforms from
// the material's QML properties - the mechanism toolpath.frag uses for
// cutoff/pathEnd. eyeRaw is the camera eye in the model raw frame (backend
// recomputes it per refresh); the view-ray cam-float used to be a CPU repack
// of the whole edge buffer on every camera move (~22 ms/frame on
// Test_model), now it is a per-vertex shift here at zero CPU cost.

VARYING float vSide;

void MAIN()
{
    vSide = UV0.x;
    // cam-float: смещение К глазу вдоль луча зрения, проекционно-нейтральное
    // (на экране позицию не двигает), выводит линию из-под отсечения буфером
    // глубины при нормальном падении взгляда. ray = точка - глаз, сдвиг -
    // минус ray, нормированный на camFloatMm (0.0 = выключен).
    vec3 vpnt = VERTEX;
    vec3 ray = vpnt - eyeRaw;
    float rl = max(length(ray), 1e-6);
    vpnt -= ray / rl * camFloatMm;
    vec4 clip = MODELVIEWPROJECTION_MATRIX * vec4(vpnt, 1.0);
    vec4 head = MODELVIEWPROJECTION_MATRIX * vec4(vpnt + NORMAL, 1.0);
    // Направление и перпендикуляр считаются в пикселях: оси NDC при
    // неквадратном вьюпорте имеют разный масштаб, и перпендикуляр,
    // построенный в NDC, давал вертикальным линиям толщину
    // lineWidthPx * viewportW / viewportH (3.0 px против 1.5 px на 800x400).
    vec2 vp = vec2(max(viewportW, 1.0), max(viewportH, 1.0));
    vec2 p0 = clip.xy / max(abs(clip.w), 1e-6) * vp * 0.5;
    vec2 p1 = head.xy / max(abs(head.w), 1e-6) * vp * 0.5;
    vec2 dir = p1 - p0;
    float dl = length(dir);
    vec2 perp = dl > 1e-6 ? vec2(-dir.y, dir.x) / dl : vec2(1.0, 0.0);
    // lineWidthPx - полная толщина: по lineWidthPx/2 на сторону; пиксели -> NDC
    // делением на половину вьюпорта, NDC -> clip умножением на w.
    clip.xy += perp * (lineWidthPx / vp) * clip.w * UV0.x;
    // Аддитивный polygon offset: сдвиг в NDC привязан к clip.w (как
    // glPolygonOffset units), поэтому не зависит от знака/величины Z и
    // держит линию перед кожей, когда кожа пишет глубину.
    clip.z -= polyOffset * clip.w;
    POSITION = clip;
}
