# Arduino CLI Setup & Usage (Linux)

## 1. Install Arduino CLI globally
```bash
# Either system-wide install via apt or local binary
sudo apt install arduino-cli
# or use local binary in your project folder
```

## 2. Initialize CLI environment in your project directory

```bash
mkdir my_project
cd my_project
arduino-cli config init
```

## 3. Point CLI to local binary (optional)

```bash
export PATH="$PWD/bin:$PATH"
# Now 'arduino-cli' will use the local binary in ./bin/
```

## 4. Install Arduino AVR core

```bash
arduino-cli core update-index
arduino-cli core install arduino:avr
```

## 5. Create a new sketch/project

```bash
arduino-cli sketch new blink
cd blink
```

## 6. Compile sketch and generate `.hex`

```bash
arduino-cli compile --fqbn arduino:avr:uno --output-dir build
```

## 7. Detect connected boards

```bash
arduino-cli board list
```

## 8. Upload sketch to board

```bash
arduino-cli upload -p /dev/ttyUSB0 --fqbn arduino:avr:uno
```

## 9. Search for library names

```bash
arduino-cli lib search "Adafruit GPS"
```

## 10. Install libraries

```bash
arduino-cli lib install "Adafruit NeoPixel"
```

## 11. List installed libraries

```bash
arduino-cli lib list
```

## 12. Monitor serial port (real-time output)

```bash
arduino-cli monitor -p /dev/ttyUSB0 -c 115200
```

## 13. How to track

```python
import math

def angle_C(ac1, ac2, ab):
    """
    Estimate angle C (at vertex C) in degrees given distances:
    ac1 = distance from A to C1
    ac2 = distance from B to C2
    ab  = distance between A and B
    Assumes small equal offset x between C1/C2 and C
    """
    x = 0.3
    ac1 += x
    ac2 += x

    # Law of cosines approximation:
    # angle_C = arccos((ac1**2 + ac2**2 - ab**2) / (2 * ac1 * ac2))
    # This treats C1 and C2 as approximating C, ignoring small x
    numerator = ac1**2 + ac2**2 - ab**2
    denominator = 2 * ac1 * ac2

    # Safety check
    cos_C = max(-1.0, min(1.0, numerator / denominator))
    angle_rad = math.acos(cos_C)
    angle_deg = math.degrees(angle_rad)

    return angle_deg

# Example values from the figure
ab = 0.40
ac1 = 0.95
ac2 = 0.62

C_angle = angle_C(ac1, ac2, ab)
print(f"Estimated angle C: {C_angle:.2f} degrees")
```