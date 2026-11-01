
import argparse
import json
import socket
import time

import krpc


PC_COMMAND_PORT = 5007
SHRIKE_TELEMETRY_PORT = 5006
RATE_HZ = 10


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--shrike-ip", required=True,
                        help="ShrikeFi's Wi-Fi IP address")
    parser.add_argument("--arm", action="store_true",
                        help="Allow ShrikeFi control commands to affect KSP")
    args = parser.parse_args()

    print("[PC] Connecting to KSP through kRPC...")
    conn = krpc.connect(name="ShrikeFi Flight Computer")
    space_center = conn.space_center
    vessel = space_center.active_vessel
    body = vessel.orbit.body

    # Use a body reference frame for meaningful velocity measurements
    flight = vessel.flight(body.reference_frame)

    telemetry_socket = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    command_socket = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    command_socket.bind(("0.0.0.0", PC_COMMAND_PORT))
    command_socket.setblocking(False)

    shrike_address = (args.shrike_ip, SHRIKE_TELEMETRY_PORT)

    print(f"[PC] Connected to vessel: {vessel.name}")
    print(f"[PC] Sending telemetry to {shrike_address}")
    print(f"[PC] Commands are {'ARMED' if args.arm else 'DISARMED'}")

    sequence = 0
    period = 1.0 / RATE_HZ

    try:
        while True:
            start = time.monotonic()
            sequence += 1

            telemetry = {
                "seq": sequence,
                "altitude": float(flight.mean_altitude),
                "vertical_speed": float(flight.vertical_speed),
                "surface_speed": float(flight.speed),
                "apoapsis": float(vessel.orbit.apoapsis_altitude),
                "periapsis": float(vessel.orbit.periapsis_altitude),
                "pitch": float(flight.pitch),
                "heading": float(flight.heading),
                "throttle": float(vessel.control.throttle),
            }

            packet = json.dumps(telemetry).encode("utf-8")
            telemetry_socket.sendto(packet, shrike_address)

            # Read any control command waiting from shrikefi
            while True:
                try:
                    data, sender = command_socket.recvfrom(512)
                except BlockingIOError:
                    break

                try:
                    command = json.loads(data.decode("utf-8"))

                    if not args.arm or command.get("enable") != 1:
                        continue

                    # Clamp all control inputs before passing them to KSP.
                    throttle = max(0.0, min(1.0,
                                           float(command["throttle"])))
                    pitch = max(-1.0, min(1.0,
                                         float(command["pitch"])))
                    yaw = max(-1.0, min(1.0,
                                       float(command["yaw"])))
                    roll = max(-1.0, min(1.0,
                                        float(command["roll"])))

                    vessel.control.throttle = throttle
                    vessel.control.pitch = pitch
                    vessel.control.yaw = yaw
                    vessel.control.roll = roll

                except (ValueError, KeyError, TypeError,
                        json.JSONDecodeError) as error:
                    print(f"[PC] Ignoring invalid command: {error}")

            print(
                f"\rAlt: {telemetry['altitude']:8.1f} m | "
                f"VSpeed: {telemetry['vertical_speed']:7.1f} m/s | "
                f"AP: {telemetry['apoapsis']:8.1f} m",
                end=""
            )

            elapsed = time.monotonic() - start
            time.sleep(max(0.0, period - elapsed))

    except KeyboardInterrupt:
        print("\n[PC] Stopping bridge.")

    finally:
        # Remove control authority when the bridge is stopped.
        vessel.control.throttle = 0.0
        vessel.control.pitch = 0.0
        vessel.control.yaw = 0.0
        vessel.control.roll = 0.0
        telemetry_socket.close()
        command_socket.close()
        conn.close()


if __name__ == "__main__":
    main()
