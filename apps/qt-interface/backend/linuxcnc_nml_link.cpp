#include "linuxcnc_backend.h"
#include "linuxcnc_link.h"

// The real link to LinuxCNC. It is built only with -DLYNX_WITH_LINUXCNC=ON, on the controller, where
// the library headers and the NML/stat libraries exist; on a development machine there is nothing to
// link against, so the factory reports that instead of inventing a connection.
//
// Task card for the stand (the mapping itself is already written and tested — LinuxCncBackend):
//
//   taskState()            <- stat.task_state                     (RCS_DONE / RCS_EXEC / RCS_ERROR)
//   interpreterState()     <- stat.interp_state                   (IDLE / READING / PAUSED / WAITING)
//   taskMode()             <- stat.task_mode                      (manual / auto / mdi)
//   estop()                <- stat.estop
//   jointPosition(j)       <- stat.position (work) or stat.joint_position (machine), joint index
//   g5xOffset(i, j)        <- stat.g5x_offset[i][j]  (i = 0..5 for G54..G59)
//   activeG5xIndex()       <- stat.g5x_index (1..6 in some builds: subtract one)
//   jointCount()           <- stat.joints
//   tool table             <- stat.tool_table[] (number, toolno, diameter, zoffset) and stat.tool_in_spindle
//   spindle() / coolant()  <- stat.spindle_enabled / stat.coolant (or the halui pins, if the build has them)
//   feedOverride()         <- stat.feedrate (percent)
//   spindleOverride()      <- stat.spindle_override_enabled ? stat.spindle_scale * 100 : 100
//   rapidOverrideIndex()   <- stat.rapid_scale -> the 5/25/50/100 row
//   singleBlock/optStop    <- stat.single_block / stat.optional_stop; dry run: stat.block_delete or the
//                             interpreter's dry-run flag, to confirm on the stand
//   operatorMessages()     <- stat.operator_message / the error channel of the NML buffer
//   programFile()          <- stat.file
//   programLines()         <- the file read from the controller's program directory
//   currentLine()/cursor() <- stat.motion_line / stat.current_line; cursor from the panel's own marker
//   commands               <- command.task_* (cycle start, pause, abort), task_mode, jump/jog,
//                             mdi, program_open, override, tool table write, g5x set/zero/touch off
//
// Until that code exists, `--backend=linuxcnc` fails with a readable reason and never falls back to
// the simulator (docs/LINUXCNC-BACKEND.md).

LinuxCncLink *createLinuxCncLink(QString *error)
{
#ifdef LYNX_WITH_LINUXCNC
    // The class and its methods are listed above; the stand build fills them in.
    if (error)
        *error = QStringLiteral("Сборка с LinuxCNC есть, но линк ещё не написан: "
                                "docs/LINUXCNC-BACKEND.md, карточка для стойки");
    return nullptr;
#else
    if (error)
        *error = QStringLiteral("сборка без LinuxCNC: на стойке пересоберите с -DLYNX_WITH_LINUXCNC=ON");
    return nullptr;
#endif
}
