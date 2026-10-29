// core/limits.h - machine limits of the demo simulation, in one place.
//
// Validation (PLAN-QUICKPROG, 8) must refuse a feed or a speed the machine
// cannot hold, and a number written into a check and a number written into a
// form drift apart. The limits therefore live here and nowhere else: the check
// reads them, the console prints them, and the form will read them too.
//
// The values are the ones the panel configuration already uses, not guesses:
// the top spindle speed and the joint velocity come from the React parameter
// tables (apps/react-interface/lib/cnc/params). The struct is the only place a
// limit is written down; wiring it to a real machine profile is the next
// slice.
#pragma once

namespace qp {

struct MachineLimits {
    // [DISPLAY].DEFAULT_SPINDLE_SPEED upper bound and the VMC850 family top
    // speed, rev/min.
    double maxRpm = 24000.0;
    // [JOINT_n].MAX_VELOCITY of X/Y is 250 mm/s (params/joints.ts), which is
    // 15000 mm/min: the fastest feed the axes hold. 250 * 60.
    double maxFeedMmMin = 15000.0;
    // G0 has no programmed feed; the estimate charges a rapid at
    // [TRAJ].MAX_LINEAR_VELOCITY (params/motion.ts, 250 mm/s = 15000 mm/min).
    // The estimate has no acceleration ramp, so it is optimistic and the
    // console says so under the time line.
    double rapidMmMin = 15000.0;
    // The tool has to be non-negative and the machine table cannot hold a tool
    // wider than the table opening; a few hundred mm is well above the demo.
    double minToolDiameterMm = 0.1;
    double maxToolDiameterMm = 200.0;
};

inline const MachineLimits& machineLimits() {
    static const MachineLimits limits;
    return limits;
}

} // namespace qp
