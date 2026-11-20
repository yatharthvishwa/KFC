
import time
import math
import krpc

# --------------------------------
# CONFIGURATION
# --------------------------------

TARGET_PERIAPSIS = 80_000       # metres
TARGET_APOAPSIS = 100_000       # metres

LAUNCH_HEADING = 90             # East
APOAPSIS_TOLERANCE = 1_500       # metres
PERIAPSIS_TOLERANCE = 1_000      # metres

# --------------------------------
# CONNECT TO KSP
# --------------------------------

conn = krpc.connect(name="ShrikeFi Orbit Controller")

vessel = conn.space_center.active_vessel
body = vessel.orbit.body
flight = vessel.flight(body.reference_frame)

autopilot = vessel.auto_pilot
control = vessel.control

print("Connected to:", vessel.name)
print("Target orbit: 80 km")

# --------------------------------
# HELPER FUNCTIONS
# --------------------------------

def clamp(value, minimum, maximum):
    return max(minimum, min(maximum, value))


def get_altitude():
    return flight.mean_altitude


def get_apoapsis():
    return vessel.orbit.apoapsis_altitude


def get_periapsis():
    return vessel.orbit.periapsis_altitude


def get_time_to_apoapsis():
    return vessel.orbit.time_to_apoapsis


def set_orientation(pitch, heading):
    autopilot.target_pitch_and_heading(pitch, heading)


def set_throttle(value):
    control.throttle = clamp(value, 0.0, 1.0)


def wait_for_stage():
    # Wait until the vessel has left the launch pad
    while get_altitude() < 5:
        time.sleep(0.1)


# --------------------------------
# AUTOPILOT SETUP
# --------------------------------

autopilot.reference_frame = vessel.surface_reference_frame
autopilot.target_roll = 0
autopilot.engage()

control.sas = False
control.rcs = False

print("Autopilot engaged")
print("Waiting for launch...")

# --------------------------------
# PHASE 1: ASCENT
# --------------------------------

print("PHASE: ASCENT")

control.activate_next_stage()
set_throttle(1.0)

wait_for_stage()

launch_time = time.time()

while True:

    elapsed = time.time() - launch_time
    altitude = get_altitude()
    apoapsis = get_apoapsis()

    # Simple gravity-turn pitch schedule.
    # Values are target pitch angles above the horizon.
    if elapsed < 5:
        target_pitch = 90
    elif elapsed < 15:
        target_pitch = 85
    elif elapsed < 25:
        target_pitch = 75
    elif elapsed < 40:
        target_pitch = 55
    elif elapsed < 55:
        target_pitch = 35
    elif elapsed < 70:
        target_pitch = 15
    else:
        target_pitch = 5

    set_orientation(target_pitch, LAUNCH_HEADING)

    # Reduce throttle as apoapsis approaches target.
    if apoapsis < TARGET_APOAPSIS - 15_000:
        throttle = 1.0
    elif apoapsis < TARGET_APOAPSIS - 5_000:
        throttle = 0.5
    else:
        throttle = 0.0

    set_throttle(throttle)

    print(
        f"ASCENT | ALT {altitude/1000:.1f} km | "
        f"APO {apoapsis/1000:.1f} km | "
        f"PITCH {target_pitch} | THR {throttle:.2f}"
    )

    if apoapsis >= TARGET_APOAPSIS:
        break

    time.sleep(0.2)

set_throttle(0)
print("Target apoapsis reached. Engine cutoff.")

# --------------------------------
# PHASE 2: COAST
# --------------------------------

print("PHASE: COAST")

while get_time_to_apoapsis() > 10:
    print(
        f"COAST | APO {get_apoapsis()/1000:.1f} km | "
        f"TTA {get_time_to_apoapsis():.1f} s"
    )
    time.sleep(1)

# Orient horizontally and prograde.
# For an eastward equatorial ascent, heading 90 is approximately east.
set_orientation(0, LAUNCH_HEADING)

while get_time_to_apoapsis() > 1:
    time.sleep(0.2)

# --------------------------------
# PHASE 3: CIRCULARIZATION
# --------------------------------

print("PHASE: CIRCULARIZATION")

set_throttle(1.0)

while True:

    periapsis = get_periapsis()
    apoapsis = get_apoapsis()

    print(
        f"CIRCULARIZE | "
        f"APO {apoapsis/1000:.1f} km | "
        f"PERI {periapsis/1000:.1f} km"
    )

    if periapsis >= TARGET_PERIAPSIS:
        break

    # If apoapsis grows too much, stop and reassess.
    if apoapsis > TARGET_APOAPSIS + 15_000:
        print("WARNING: Apoapsis overshoot. Stopping burn.")
        break

    time.sleep(0.2)

set_throttle(0)

print("--------------------------------")
print("AUTONOMOUS ORBIT INSERTION ENDED")
print("Apoapsis:", get_apoapsis())
print("Periapsis:", get_periapsis())
print("--------------------------------")
