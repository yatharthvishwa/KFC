import krpc
import time

conn = krpc.connect(
    name="Flight Computer"
)

vessel = conn.space_center.active_vessel
flight = vessel.flight()

print("Connected to:", vessel.name)

while True:
    print(
        f"Altitude: {flight.mean_altitude:.1f} m | "
        f"Speed: {flight.speed:.1f} m/s | "
        f"Vertical speed: {flight.vertical_speed:.1f} m/s"
    )
    time.sleep(0.1)