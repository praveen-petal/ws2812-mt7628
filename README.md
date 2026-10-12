# MediaTek MT7628 WS2812 NeoPixel Kernel Driver (OpenWrt 23.05.5)

Kernel driver and automated build pipeline for the **24-LED WS2812B RGB Ring** on **Minew G1 / Petals Gateway** (MediaTek MT7628AN SoC, GPIO 11 DIN).

---

## 🎯 Hardware Specifications
* **SoC:** MediaTek MT7628AN (`ramips/mt76x8`, `mipsel_24kc`)
* **OS:** OpenWrt 23.05.5 (Kernel `5.15.167`)
* **Pin:** GPIO 11 (Pin 491, Direct Register `0xb0000600`)
* **LED Ring:** 24 Addressable WS2812B RGB LEDs (Single line 800 kHz bitbang with IRQ lock)
* **Device Node:** `/dev/ws2812` (Major 152)

---

## 🚀 How to Build via GitHub Actions (Zero Local Setup)

1. Create a new empty repository on your GitHub account (e.g., `ws2812-mt7628`).
2. Open terminal in this folder (`ws2812_build_package`):
   ```bash
   cd ws2812_build_package
   git init
   git add .
   git commit -m "Add MT7628 WS2812 driver for OpenWrt 23.05.5"
   git branch -M main
   git remote add origin https://github.com/<YOUR_GITHUB_USERNAME>/ws2812-mt7628.git
   git push -u origin main
   ```
3. Go to your GitHub repository -> Click on **Actions** tab.
4. You will see **Build OpenWrt WS2812 Driver** running. It compiles automatically in ~2-3 minutes.
5. Once completed, click the workflow run -> Download the artifact **`ws2812-mt7628-openwrt-23.05.5`** (contains `ws2812-draiveris.ko` and `.ipk`).

---

## 📦 Installation on Router

1. Copy `ws2812-draiveris.ko` to the gateway router:
   ```bash
   scp ws2812-draiveris.ko root@192.168.1.199:/root/
   ```

2. SSH into the router:
   ```bash
   ssh root@192.168.1.199
   ```

3. Load the kernel module:
   ```bash
   insmod /root/ws2812-draiveris.ko
   ```
   *(If `/dev/ws2812` is not created automatically by devtmpfs, create it once: `mknod /dev/ws2812 c 152 0`)*

4. Verify kernel output:
   ```bash
   dmesg | tail -n 10
   ```
   You should see:
   ```text
   ws2812: Major=152 device=/dev/ws2812
   ws2812: Base GPIO: 11, count: 1, leds_per_gpio: 24
   ```

---

## 💡 Quick LED Tests

### 🔴 Red Light (All 24 LEDs)
```bash
python3 -c "import os; f=open('/dev/ws2812','wb'); f.write(bytes([255,0,0]*24)); f.close()"
```

### 🟢 Green Light
```bash
python3 -c "import os; f=open('/dev/ws2812','wb'); f.write(bytes([0,255,0]*24)); f.close()"
```

### 🔵 Blue Light
```bash
python3 -c "import os; f=open('/dev/ws2812','wb'); f.write(bytes([0,0,255]*24)); f.close()"
```

### 🌸 Petals Automation Maroon (`#800000`)
```bash
python3 -c "import os; f=open('/dev/ws2812','wb'); f.write(bytes([128,0,32]*24)); f.close()"
```

### 🌈 Rainbow Chase / Breathing
```python
import time, math

def wheel(pos):
    pos = 255 - pos
    if pos < 85:
        return (255 - pos * 3, 0, pos * 3)
    if pos < 170:
        pos -= 85
        return (0, pos * 3, 255 - pos * 3)
    pos -= 170
    return (pos * 3, 255 - pos * 3, 0)

with open('/dev/ws2812', 'wb') as f:
    for j in range(256):
        data = bytearray()
        for i in range(24):
            r, g, b = wheel((int(i * 256 / 24) + j) & 255)
            # Scale brightness to 30%
            data.extend([int(r*0.3), int(g*0.3), int(b*0.3)])
        f.write(data)
        f.flush()
        time.sleep(0.02)
```

### ⬛ Turn Off All LEDs
```bash
python3 -c "import os; f=open('/dev/ws2812','wb'); f.write(bytes([0,0,0]*24)); f.close()"
```

---

## 🔄 Auto-Load on Boot

To load the driver automatically every time OpenWrt boots:
```bash
cp /root/ws2812-draiveris.ko /lib/modules/5.15.167/
echo "ws2812-draiveris" > /etc/modules.d/92-ws2812
```
