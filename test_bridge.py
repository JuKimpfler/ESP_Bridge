"""
ESP-NOW UART Bridge – Dual Serial Monitor & Auto-Pairing Test
Connects to both ESP32-C3 modules, reads boot output,
performs pairing via ET+ commands, and verifies the connection.
"""

import serial
import time
import threading
import sys
import re

PORT_A = "COM21"
PORT_B = "COM22"
BAUD = 115200
TIMEOUT = 1  # serial read timeout in seconds

# Collected output per port
output_a = []
output_b = []
lock = threading.Lock()

def read_serial(ser, label, output_list, stop_event):
    """Continuously read from a serial port and print + store lines."""
    while not stop_event.is_set():
        try:
            line = ser.readline()
            if line:
                decoded = line.decode("utf-8", errors="replace").rstrip("\r\n")
                with lock:
                    output_list.append(decoded)
                    print(f"[{label}] {decoded}")
        except serial.SerialException:
            print(f"[{label}] *** Port lost ***")
            break
        except Exception as e:
            print(f"[{label}] Error: {e}")
            break

def send_cmd(ser, cmd, label):
    """Send a command string followed by \\r\\n."""
    print(f"\n>>> Sending to {label}: {cmd}")
    ser.write((cmd + "\r\n").encode())
    time.sleep(0.3)

def wait_for(output_list, pattern, timeout=10):
    """Wait until a line matching `pattern` appears in output_list."""
    start = time.time()
    while time.time() - start < timeout:
        with lock:
            for line in output_list:
                if re.search(pattern, line):
                    return line
        time.sleep(0.2)
    return None

def extract_mac(output_list):
    """Extract own MAC address from boot output."""
    with lock:
        for line in output_list:
            m = re.search(r"Eigene MAC:\s+([0-9A-Fa-f:]{17})", line)
            if m:
                return m.group(1)
    return None

def extract_peer_mac(output_list):
    """Extract stored peer MAC from boot output."""
    with lock:
        for line in output_list:
            m = re.search(r"Gespeicherte Peer-MAC:\s+([0-9A-Fa-f:]{17})", line)
            if m:
                return m.group(1)
            m = re.search(r"Peer MAC:\s+([0-9A-Fa-f:]{17})", line)
            if m:
                return m.group(1)
    return None

def main():
    print("=" * 60)
    print("  ESP-NOW Bridge – Dual Monitor & Connection Test")
    print(f"  Module A: {PORT_A}   Module B: {PORT_B}")
    print("=" * 60)

    # Open both ports
    try:
        ser_a = serial.Serial(PORT_A, BAUD, timeout=TIMEOUT)
    except Exception as e:
        print(f"Cannot open {PORT_A}: {e}")
        sys.exit(1)
    try:
        ser_b = serial.Serial(PORT_B, BAUD, timeout=TIMEOUT)
    except Exception as e:
        print(f"Cannot open {PORT_B}: {e}")
        ser_a.close()
        sys.exit(1)

    stop = threading.Event()
    t_a = threading.Thread(target=read_serial, args=(ser_a, PORT_A, output_a, stop), daemon=True)
    t_b = threading.Thread(target=read_serial, args=(ser_b, PORT_B, output_b, stop), daemon=True)
    t_a.start()
    t_b.start()

    # Wait for boot messages (modules reset after flash)
    print("\n--- Waiting for boot output (5 s) ---")
    time.sleep(5)

    # Extract MACs
    mac_a = extract_mac(output_a)
    mac_b = extract_mac(output_b)
    print(f"\n=== MAC addresses ===")
    print(f"  Module A ({PORT_A}): {mac_a or 'NOT FOUND'}")
    print(f"  Module B ({PORT_B}): {mac_b or 'NOT FOUND'}")

    if not mac_a or not mac_b:
        print("\n[ERROR] Could not read MAC addresses. Aborting.")
        stop.set()
        ser_a.close()
        ser_b.close()
        return

    # Check if already paired
    peer_a = extract_peer_mac(output_a)
    peer_b = extract_peer_mac(output_b)
    already_paired = (peer_a and peer_b and 
                      peer_a.upper() == mac_b.upper() and 
                      peer_b.upper() == mac_a.upper())

    if already_paired:
        print("\n[OK] Modules are already paired correctly!")
        print(f"  A's peer = {peer_a} (should be B's MAC {mac_b})")
        print(f"  B's peer = {peer_b} (should be A's MAC {mac_a})")
    else:
        print("\n--- Pairing modules ---")
        
        # === Pair Module A → set peer to B's MAC ===
        send_cmd(ser_a, "ET+OPEN", PORT_A)
        time.sleep(1)
        send_cmd(ser_a, f"ET+PEER={mac_b}", PORT_A)
        time.sleep(0.5)
        send_cmd(ser_a, "ET+SAVE", PORT_A)
        time.sleep(1)

        # === Pair Module B → set peer to A's MAC ===
        send_cmd(ser_b, "ET+OPEN", PORT_B)
        time.sleep(1)
        send_cmd(ser_b, f"ET+PEER={mac_a}", PORT_B)
        time.sleep(0.5)
        send_cmd(ser_b, "ET+SAVE", PORT_B)
        time.sleep(1)

    # Wait for connection to establish via heartbeat
    print("\n--- Waiting for ESP-NOW connection (up to 15 s) ---")
    connected_a = wait_for(output_a, r"VERBUNDEN", timeout=15)
    connected_b = wait_for(output_b, r"VERBUNDEN", timeout=15)

    print(f"\n=== Connection Status ===")
    print(f"  Module A: {'CONNECTED' if connected_a else 'NOT CONNECTED'}")
    print(f"  Module B: {'CONNECTED' if connected_b else 'NOT CONNECTED'}")

    # Query status from both
    print("\n--- Querying status ---")
    send_cmd(ser_a, "ET+STATUS?", PORT_A)
    time.sleep(1)
    send_cmd(ser_b, "ET+STATUS?", PORT_B)
    time.sleep(1)

    # Check for Verbunden: JA in status output
    verbunden_a = wait_for(output_a, r"Verbunden:\s+JA", timeout=3)
    verbunden_b = wait_for(output_b, r"Verbunden:\s+JA", timeout=3)

    print(f"\n{'=' * 60}")
    print(f"  FINAL RESULT")
    print(f"{'=' * 60}")
    print(f"  Module A ({PORT_A}): MAC={mac_a}")
    print(f"    → Peer connected: {'YES' if verbunden_a else 'NO'}")
    print(f"  Module B ({PORT_B}): MAC={mac_b}")
    print(f"    → Peer connected: {'YES' if verbunden_b else 'NO'}")

    if verbunden_a and verbunden_b:
        print(f"\n  ✓ BRIDGE IS WORKING – both modules connected via ESP-NOW!")
    else:
        print(f"\n  ✗ Connection issue – check output above for details.")
    print(f"{'=' * 60}")

    # Keep monitoring for a few more seconds to see heartbeat traffic
    print("\n--- Monitoring heartbeat traffic (10 s) ---")
    time.sleep(10)

    # Final summary
    print("\n--- Done. Closing ports. ---")
    stop.set()
    time.sleep(0.5)
    ser_a.close()
    ser_b.close()

if __name__ == "__main__":
    main()
