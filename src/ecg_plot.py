import serial
import matplotlib.pyplot as plt
from collections import deque

# --- CONFIG ---
PORT = 'COM4'        # <-- Change this to your actual COM port
BAUD_RATE = 115200
BUFFER_SIZE = 200

# --- SERIAL INIT ---
ser = serial.Serial(PORT, BAUD_RATE)
print(f"Connected to {PORT}")

# --- PLOT INIT ---
plt.ion()
buffer = deque([0]*BUFFER_SIZE, maxlen=BUFFER_SIZE)
fig, ax = plt.subplots()
line, = ax.plot(buffer)
ax.set_ylim(0, 4095)
ax.set_title("ECG Signal (ESP32 + AD8232)")
ax.set_ylabel("ADC Value")
ax.set_xlabel("Sample")

# --- PLOTTING LOOP ---
try:
    while True:
        raw = ser.readline()
        try:
            value = int(raw.decode().strip())
            buffer.append(value)
            line.set_ydata(buffer)
            line.set_xdata(range(len(buffer)))
            ax.relim()
            ax.autoscale_view()
            fig.canvas.draw()
            fig.canvas.flush_events()
            plt.pause(0.001)
        except ValueError:
            pass  # Ignore invalid lines

except KeyboardInterrupt:
    print("Exiting...")
    ser.close()
    plt.close()
