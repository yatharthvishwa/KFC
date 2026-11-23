
import argparse
import json
import socket
import time

import krpc


PC_COMMAND_PORT = 5007
SHRIKE_TELEMETRY_PORT = 5006
RATE_HZ = 10

def clamp(value, minimum, maximum):
    return max(minimum, min(maximum, value))

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--shrike-ip", required=True,
                        help="ShrikeFi's Wi-Fi IP address")
    parser.add_argument("--arm", action="store_true",
                        help="Allow ShrikeFi control commands to affect KSP")
    args = parser.parse_args()

    print("[PC] Connecting to KSP through kRPC...")
    conn = krpc.connect(name="K F C")
    space_center = conn.space_center

    vessel = space_center.active_vessel
    auto_pilot = vessel.auto_pilot
    auto_pilot.reference_frame = vessel.surface_reference_frame
    auto_pilot.target_roll = 0
    # auto_pilot.engaged = True

    body = vessel.orbit.body

    # Use a body reference frame for meaningful velocity measurements
    flight = vessel.flight(body.reference_frame)

    telemetry_socket = socket.socket(socket.AF_INET, socket.SOCK_DGRAM) #sending UDP socket creation
    command_socket = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)   #receiving UDP socket creation
    command_socket.bind(("0.0.0.0", PC_COMMAND_PORT))
    command_socket.setblocking(False)

    shrike_address = (args.shrike_ip, SHRIKE_TELEMETRY_PORT)

    print(f"[PC] Connected to vessel: {vessel.name}")
    print(f"[PC] Sending telemetry to {shrike_address}")
    print(f"[PC] Commands are {'ARMED' if args.arm else 'DISARMED'}")

    sequence = 0
    period = 1.0 / RATE_HZ
    last_stage_time = 0.0   # before the loop

    try:
        while True:
            start = time.monotonic()
            sequence += 1


            # read the flight data from ksp
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
                "time_to_apoapsis": float(vessel.orbit.time_to_apoapsis),
            }

            packet = json.dumps(telemetry).encode("utf-8") #packet creation json string to utf8 bytes
            telemetry_socket.sendto(packet, shrike_address)#packet transmission
            print(f"[PC] UDP sent to {shrike_address}: {packet.decode('utf-8')}")

            # Read any control command waiting from shrikefi
            while True:
                try:
                    data, sender = command_socket.recvfrom(512) #packet receive 512 bytes
                    print(f"[PC] UDP received from {sender}: {data.decode('utf-8', errors='replace')}")
                except BlockingIOError:
                    break

                try:
                    command = json.loads(data.decode("utf-8"))  #packet decoding

                    if not args.arm or command.get("enable") != 1:
                        continue

                    # Clamp all control inputs before passing them to KSP.
                    throttle = clamp(float(command["throttle"]), 0, 1)

                    pitch    = clamp(float(command["target_pitch"]), 0, 90)

                    heading  = float(command["heading"])

                    vessel.control.throttle = throttle

                    if not auto_pilot.engaged:
                        vessel.control.sas = False
                        auto_pilot.engaged = True

                    auto_pilot.target_pitch_and_heading(pitch, heading)

                    print(
                        f"[PC] Applied command: throttle={throttle:.2f}, "
                        f"target_pitch={pitch:.1f}, heading={heading:.1f}"
                    )

                    if throttle > 0 and vessel.available_thrust == 0 and time.monotonic() - last_stage_time > 1.5:
                        vessel.control.activate_next_stage()
                        last_stage_time = time.monotonic()

                except (ValueError, KeyError, TypeError,
                        json.JSONDecodeError) as error:
                    print(f"[PC] Ignoring invalid command: {error}")

            print(
                f"[PC] Alt: {telemetry['altitude']:8.1f} m | "
                f"VSpeed: {telemetry['vertical_speed']:7.1f} m/s | "
                f"AP: {telemetry['apoapsis']:8.1f} m"
            )

            elapsed = time.monotonic() - start
            time.sleep(max(0.0, period - elapsed))

    except KeyboardInterrupt:
        print("\n[PC] Stopping bridge.")

    finally:
        # Remove control authority when the bridge is stopped.
        vessel.control.throttle = 0.0
        auto_pilot.engaged  = False
        vessel.control.pitch = 0.0
        vessel.control.yaw = 0.0
        vessel.control.roll = 0.0
        telemetry_socket.close()
        command_socket.close()
        conn.close()


if __name__ == "__main__":
    main()
