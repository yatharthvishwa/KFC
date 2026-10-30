
import krpc
import time
import csv
from datetime import datetime

print("Connecting to KSP...")

conn = krpc.connect(name="ShrikeFi Flight Computer")

vessel = conn.space_center.active_vessel
flight = vessel.flight()

body = vessel.orbit.body

print("CONNECTED!")
print("Vessel:", vessel.name)
print("Celestial body:", body.name)

with open("flight_data.csv", "w", newline="") as file:

    writer = csv.writer(file)

    writer.writerow([
        "timestamp",
        "mission_time",
        "altitude",
        "surface_speed",
        "vertical_speed",
        "pitch",
        "heading",
        "throttle",
        "apoapsis",
        "periapsis"
    ])

    try:
        while True:

            altitude = flight.mean_altitude
            speed = flight.speed
            vertical_speed = flight.vertical_speed

            pitch = flight.pitch
            heading = flight.heading

            throttle = vessel.control.throttle

            apoapsis = vessel.orbit.apoapsis_altitude
            periapsis = vessel.orbit.periapsis_altitude

            mission_time = conn.space_center.ut

            print(
                f"ALT: {altitude:8.1f} m | "
                f"SPD: {speed:7.1f} m/s | "
                f"VSPD: {vertical_speed:7.1f} m/s | "
                f"PITCH: {pitch:6.1f}° | "
                f"AP: {apoapsis:8.1f} m"
            )

            writer.writerow([
                datetime.now().isoformat(),
                mission_time,
                altitude,
                speed,
                vertical_speed,
                pitch,
                heading,
                throttle,
                apoapsis,
                periapsis
            ])

            file.flush()

            time.sleep(0.1)

    except KeyboardInterrupt:
        print("\nFlight recording stopped.")
