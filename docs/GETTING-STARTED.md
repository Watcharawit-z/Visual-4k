# คู่มือติดตั้งและใช้งานแบบละเอียด

เอกสารนี้พาไปทีละขั้นตั้งแต่เครื่องเปล่าจนใช้งานได้จริง อ่านส่วน "ความเสี่ยง" ก่อน
เสมอ — มีสองจุดที่ทำผิดแล้วอาจเข้า Windows ไม่ได้

**เวลาที่ใช้โดยประมาณ**

| ขั้น | เวลา |
|---|---|
| ขั้น 0 — ลองดูผลก่อน ไม่ติดตั้งอะไร | 5 นาที |
| ขั้น 1 — ติดตั้งเครื่องมือพัฒนา | 30–90 นาที (ดาวน์โหลดใหญ่) |
| ขั้น 2 — build | 5 นาที |
| ขั้น 3 — เปิด test signing + reboot | 10 นาที |
| ขั้น 4 — ติดตั้งไดรเวอร์ | 5 นาที |
| ขั้น 5 — ตั้งค่าจอและใช้งาน | 10 นาที |

---

## ความเสี่ยง — อ่านก่อน

### 1. BitLocker อาจขอ recovery key

`bcdedit /set testsigning on` **แก้ boot configuration** ถ้าเครื่องคุณเปิด BitLocker
ไว้และผูกกับ TPM การแก้ boot config อาจทำให้ครั้งต่อไปที่บูต Windows ขอ
**BitLocker recovery key** ถ้าไม่มีคีย์ = เข้าเครื่องไม่ได้ และข้อมูลหายถาวร

**ก่อนทำอะไรทั้งสิ้น ตรวจสอบ:**

```powershell
manage-bde -status C:
```

ถ้าขึ้น `Protection On` ให้ทำอย่างใดอย่างหนึ่ง:

```powershell
# หา recovery key แล้วจดไว้ที่อื่น (โทรศัพท์ กระดาษ)
manage-bde -protectors -get C:

# หรือหยุด BitLocker ชั่วคราวข้ามการ reboot 2 ครั้ง (ปลอดภัยกว่า)
manage-bde -protectors -disable C: -RebootCount 2
```

recovery key ของบัญชี Microsoft ดูได้ที่ https://aka.ms/myrecoverykey

### 2. Secure Boot อาจบล็อก test signing

ถ้า Secure Boot เปิดอยู่ `bcdedit /set testsigning on` จะสำเร็จแต่ **ไม่มีผล** —
Windows ยังปฏิเสธไดรเวอร์อยู่ดี ต้องปิด Secure Boot ใน UEFI/BIOS ซึ่ง**ก็ทำให้
BitLocker ขอ recovery key ได้เช่นกัน** ตรวจสถานะ:

```powershell
Confirm-SecureBootUEFI
```

`True` = เปิดอยู่ ต้องปิดใน UEFI (กด Del/F2/F12 ตอนบูต ต่างกันตามยี่ห้อ)

### 3. test signing ลดความปลอดภัยของทั้งเครื่อง

ขณะที่เปิดอยู่ Windows จะยอมโหลดไดรเวอร์ **ใด ๆ** ที่เซ็นด้วย certificate ที่อยู่ใน
trust store ของเครื่อง ไม่ใช่แค่ตัวนี้ อย่าเปิดทิ้งไว้บนเครื่องที่ใช้งานจริงหรือเก็บ
ข้อมูลสำคัญ ปิดกลับเมื่อเลิกทดลอง

### 4. สร้าง restore point ก่อน

```powershell
# PowerShell แบบ Administrator
Enable-ComputerRestore -Drive "C:\"
Checkpoint-Computer -Description "ก่อนติดตั้ง Visual-4k" -RestorePointType MODIFY_SETTINGS
```

---

## ขั้น 0 — ลองดูผลก่อน โดยไม่ติดตั้งอะไร

ทำอันนี้ก่อนเสมอ ใช้เวลา 5 นาที และตอบคำถามว่า "คุ้มไหม" ก่อนจะไปยุ่งกับไดรเวอร์

### 0.1 ติดตั้ง Python

ถ้ายังไม่มี ดาวน์โหลดจาก https://www.python.org/downloads/
**ติ๊ก "Add python.exe to PATH" ตอนติดตั้ง** ไม่งั้นคำสั่งข้างล่างจะไม่เจอ

```powershell
python --version        # ควรได้ 3.9 ขึ้นไป
pip install numpy pillow
```

### 0.2 ดาวน์โหลดโปรเจกต์

```powershell
cd $HOME\Documents
git clone https://github.com/Watcharawit-z/Visual-4k.git
cd Visual-4k
git checkout claude/2k-to-4k-upscaling-l7ax1l
```

ไม่มี git ก็ดาวน์โหลด ZIP จากหน้า GitHub แล้วแตกไฟล์ได้

### 0.3 ดู scene สังเคราะห์

```powershell
python tools\visual4k.py demo demo.png
```

เปิด `demo.png` — สามภาพซ้อนกัน บนสุดคือจอ 1440p ปกติ กลางคือ Visual-4k
ล่างสุดคือความจริง สังเกตวงแหวน moiré ที่มั่วในภาพบน กับเส้นตรงกลางที่แตกเป็นขั้น

### 0.4 ลองกับภาพของคุณเอง

หาภาพ 4K มาสักภาพ — วอลเปเปอร์ 4K, ภาพถ่ายจากกล้อง, หรือสกรีนช็อตเกมจาก
เครื่องที่มีจอ 4K ยิ่งมีรายละเอียดละเอียดยิบ (ตัวหนังสือเล็ก ใบไม้ ผ้าลายตาราง)
ยิ่งเห็นความต่างชัด

```powershell
python tools\visual4k.py compare ภาพ4K.png ผลลัพธ์.png
```

จะได้ตัวเลข PSNR/SSIM ออกมาทางหน้าจอ และภาพซ้อนสามชั้นให้เทียบ

> **หมายเหตุ:** ขั้นนี้วัดคุณภาพของ *ฟิลเตอร์* เท่านั้น ไม่ได้จำลองการเรนเดอร์ที่ 4K จริง
> ประโยชน์จริงของโปรเจกต์นี้มากกว่าที่เห็นในขั้นนี้ เพราะของจริง GPU จะ rasterize
> เรขาคณิตที่ 4K ไม่ใช่แค่ย่อภาพที่มีอยู่แล้ว

**ถ้าดูแล้วไม่ประทับใจ หยุดตรงนี้ได้เลย** ไม่ต้องเสียเวลากับไดรเวอร์

---

## ขั้น 1 — เตรียมเครื่อง

### 1.1 ตรวจว่า Windows รองรับ

```powershell
[System.Environment]::OSVersion.Version
```

ต้องเป็น build **16299 ขึ้นไป** (Windows 10 เวอร์ชัน 1709 / ตุลาคม 2017)
IddCx ไม่มีในรุ่นเก่ากว่านี้

### 1.2 Visual Studio 2022

ดาวน์โหลด Community edition (ฟรี) จาก https://visualstudio.microsoft.com/downloads/

ตอนติดตั้ง **ต้องติ๊ก workload:**
- ☑ **Desktop development with C++**

และในแท็บ Individual components ตรวจว่ามี:
- ☑ **Windows 11 SDK** (หรือ Windows 10 SDK) — เวอร์ชันล่าสุด
- ☑ **MSVC v143 - VS 2022 C++ x64/x86 build tools**
- ☑ **C++ CMake tools for Windows**

### 1.3 Windows Driver Kit (WDK)

**นี่คือส่วนที่คนพลาดบ่อยที่สุด: เวอร์ชัน WDK ต้องตรงกับ SDK**

ดาวน์โหลดจาก https://learn.microsoft.com/windows-hardware/drivers/download-the-wdk

หน้านั้นจะบอกว่า WDK เวอร์ชันนี้ต้องใช้ SDK เวอร์ชันไหน ถ้าไม่ตรงจะ build ไม่ผ่าน
ด้วย error ที่ไม่บอกสาเหตุ ติดตั้งตามลำดับนี้:

1. Visual Studio 2022 (ข้อ 1.2)
2. Windows SDK เวอร์ชันที่ WDK ต้องการ
3. WDK — ตัวติดตั้งจะลง **WDK Visual Studio extension** ให้ด้วย **อย่าข้าม**

ตรวจว่าติดตั้งสำเร็จ: เปิด Visual Studio → File → New → Project → พิมพ์ "driver"
ถ้าเห็น template ชื่อ **"User Mode Driver (UMDF V2)"** แปลว่าพร้อมแล้ว

### 1.4 CMake และ Git

```powershell
winget install Kitware.CMake
winget install Git.Git
```

ปิดแล้วเปิด PowerShell ใหม่ เพื่อให้ PATH อัปเดต แล้วตรวจ:

```powershell
cmake --version         # 3.20 ขึ้นไป
git --version
```

---

## ขั้น 2 — build

```powershell
cd $HOME\Documents\Visual-4k
.\tools\build.ps1
```

สคริปต์จะ:
1. build คอมโพสิเตอร์ (`visual4k-host.exe`)
2. รัน self-test ของ EDID, tap table และตัวถอดรหัสเคอร์เซอร์
3. build ไดรเวอร์ ถ้ามี WDK

### สิ่งที่ควรเห็น

```
==> Building visual4k-host (Release)
  compositor: ...\build\host\visual4k-host\Release\visual4k-host.exe

==> Running the portable self-tests
  100% tests passed

==> Building Visual4kDisplay
  driver built. Install it with: .\tools\install-driver.ps1
```

### ถ้า build ไม่ผ่าน

| ข้อความ | สาเหตุ |
|---|---|
| `cmake not found` | ยังไม่ได้ติดตั้ง CMake หรือยังไม่ได้เปิด PowerShell ใหม่ |
| `MSBuild not found` | ยังไม่ได้ติดตั้ง Visual Studio หรือขาด workload C++ |
| `WindowsUserModeDriver10.0 toolset not found` | ยังไม่ได้ติดตั้ง WDK หรือขาด VS extension ของ WDK |
| `cannot open source file "iddcx.h"` | WDK เวอร์ชันไม่ตรงกับ SDK ติดตั้งใหม่ตามลำดับข้อ 1.3 |
| error ในไฟล์ `.cpp` | โค้ดนี้ยังไม่เคยผ่านคอมไพเลอร์จริง ส่ง error มาได้ |

> **คอมโพสิเตอร์ใช้ได้แม้ไม่มีไดรเวอร์** ถ้า build ไดรเวอร์ไม่ผ่าน แต่คอมโพสิเตอร์ผ่าน
> คุณยังทดสอบกับจอที่สองที่ความละเอียดสูงกว่าพาเนลได้ (ดูขั้น 6.3)

---

## ขั้น 3 — เปิด test signing

**อ่านส่วน "ความเสี่ยง" ข้างบนให้จบก่อน** โดยเฉพาะเรื่อง BitLocker

เปิด **PowerShell แบบ Administrator** (คลิกขวาที่ Start → Terminal (Admin))

```powershell
cd $HOME\Documents\Visual-4k

# ดูก่อนว่าจะเกิดอะไรขึ้น ยังไม่เปลี่ยนอะไรจริง
.\tools\install-driver.ps1 -WhatIf
```

สคริปต์จะบอกว่า test signing ปิดอยู่และหยุด — **มันจะไม่เปิดให้เอง** เมื่อพร้อมแล้ว:

```powershell
.\tools\install-driver.ps1 -EnableTestSigning
```

แล้ว **reboot**

หลัง reboot จะเห็นข้อความ "Test Mode" ที่มุมขวาล่างของเดสก์ท็อป — นั่นคือสัญญาณว่า
สำเร็จ ตรวจซ้ำได้ด้วย:

```powershell
bcdedit /enum '{current}' | Select-String testsigning
```

ต้องได้ `testsigning   Yes` ถ้าได้ `No` ทั้งที่สั่งไปแล้ว แปลว่า Secure Boot กำลังบล็อก
ต้องปิด Secure Boot ใน UEFI ก่อน

---

## ขั้น 4 — ติดตั้งไดรเวอร์

ใน **PowerShell แบบ Administrator** อีกครั้ง:

```powershell
cd $HOME\Documents\Visual-4k
.\tools\install-driver.ps1
```

สคริปต์จะ:
1. สร้าง self-signed certificate (หรือใช้ตัวเดิมถ้ามีแล้ว)
2. ใส่ certificate ลง trust store ของเครื่อง (Root และ TrustedPublisher)
3. สร้างและเซ็น catalog ด้วย `inf2cat` + `signtool`
4. ติดตั้งด้วย `pnputil` แล้วสร้าง device ด้วย `devcon`

### ตรวจว่าสำเร็จ

เปิด **Device Manager** (`devmgmt.msc`) → **Display adapters**
ควรเห็น **"Visual-4k Virtual Display"**

- ถ้ามีเครื่องหมายตกใจสีเหลือง → ดับเบิลคลิกดู error code (ตารางแก้ปัญหาข้างล่าง)
- ถ้าไม่เห็นเลย → ไดรเวอร์ staged แล้วแต่ยังไม่ได้สร้าง device สร้างเองที่
  Device Manager → Action → Add legacy hardware → Install manually →
  Display adapters → Have Disk → ชี้ไปที่ `Visual4kDisplay.inf`

---

## ขั้น 5 — ตั้งค่าจอ

**ตรงนี้มีกับดัก อ่านให้จบก่อนลงมือ**

### เข้าใจก่อนว่าจะเกิดอะไรขึ้น

เมื่อตั้งค่าเสร็จ เดสก์ท็อปจริง ๆ ที่คุณทำงานอยู่จะอยู่บน **จอเสมือน 4K ที่มองไม่เห็น**
ส่วนพาเนลจริงจะกลายเป็น "ช่องมอง" ที่ `visual4k-host` วาดภาพจากจอเสมือนลงไป

แปลว่า **ถ้ายังไม่ได้รัน `visual4k-host` คุณจะมองไม่เห็นเดสก์ท็อปของตัวเอง**

**ตาข่ายนิรภัยที่ Windows มีให้:** ทุกครั้งที่เปลี่ยนการตั้งค่าจอ Windows จะถามว่า
"Keep these changes?" ถ้าไม่กดยืนยันภายใน **15 วินาที** มันจะย้อนกลับเอง — ฉะนั้น
ถ้าจอดำ **แค่รอ 15 วินาที** อย่าตกใจ อย่ารีสตาร์ต

### ลำดับที่ปลอดภัย

1. เปิด **Settings → System → Display** จะเห็นจอสองตัว

2. **รัน `visual4k-host` ก่อน** ตอนนี้จอเสมือนยังไม่ใช่จอหลัก และยังตั้งความละเอียด
   ไม่ถูก แต่รันเพื่อให้แน่ใจว่าโปรแกรมทำงานได้:

   ```powershell
   .\build\host\visual4k-host\Release\visual4k-host.exe --list-displays
   ```

   ควรเห็นจอเสมือนในรายการ จำชื่อ `\\.\DISPLAYn` ไว้

3. คลิกที่จอเสมือนใน Settings → ตั้ง **Display resolution = 3840 × 2160**
   → กด **Keep changes**

4. ยังอยู่ที่จอเสมือน → เลื่อนลงหา **Multiple displays** → ติ๊ก
   **Make this my main display**
   → กด **Keep changes** ภายใน 15 วินาที

   *ตอนนี้พาเนลจริงจะดูเหมือนว่างเปล่า เพราะเดสก์ท็อปย้ายไปอยู่จอเสมือนแล้ว*

5. ตรวจว่าโหมดเป็น **Extend these displays** ไม่ใช่ Duplicate
   (Duplicate จะทำให้ภาพวนเป็นวงกลม)

6. รันคอมโพสิเตอร์:

   ```powershell
   .\build\host\visual4k-host\Release\visual4k-host.exe
   ```

   พาเนลจริงจะแสดงภาพของเดสก์ท็อป 4K ที่ย่อลงมาแล้ว พร้อมเมาส์

### ถ้าจอดำและ 15 วินาทีผ่านไปแล้ว

1. กด **Win + P** แล้วกดลูกศรลง + Enter เพื่อสลับโหมดจอแบบมองไม่เห็น
2. ถ้าไม่ได้ผล กด **Ctrl+Alt+F12** (ปิด visual4k-host) แล้วลอง Win+P ใหม่
3. ถ้ายังไม่ได้ บูตเข้า **Safe Mode** แล้วถอนไดรเวอร์:
   - กด Shift ค้างขณะคลิก Restart
   - Troubleshoot → Advanced options → Startup Settings → Restart → กด 4
   - เข้า Safe Mode แล้วรัน `.\tools\uninstall-driver.ps1`

---

## ขั้น 6 — ใช้งานและปรับจูน

### 6.1 ปุ่มลัด

| ปุ่ม | ผล |
|---|---|
| **Ctrl + Alt + F12** | ปิดคอมโพสิเตอร์ — ใช้ได้จากทุกที่ |
| **Esc** | ปิด (เฉพาะตอนหน้าต่างมี focus) |

Ctrl+Alt+F12 สำคัญกว่าที่คิด เพราะพอคลิกเข้าเดสก์ท็อปเสมือนแล้ว focus จะหลุด
และ Esc จะไปไม่ถึง

### 6.2 ตัวเลือกตามประเภทเนื้อหา

```powershell
# เดสก์ท็อป ตัวหนังสือ งานเอกสาร — คมที่สุด
visual4k-host.exe --kernel lanczos2 --sharpness 0.0

# เกม — ค่า default สมดุลระหว่างคมกับ ringing
visual4k-host.exe --kernel lanczos2 --sharpness 0.25

# วิดีโอ ภาพยนตร์ — นุ่มกว่า ลด noise, resolve ในพื้นที่เชิงเส้น
visual4k-host.exe --kernel lanczos3 --sharpness 0.5 --denoise --linear

# ปิด vsync ถ้าอยากได้ latency ต่ำสุดและยอมรับ tearing
visual4k-host.exe --vsync 0

# ปิดการวาดเคอร์เซอร์ (เช่นตอนอัดวิดีโอ)
visual4k-host.exe --no-cursor
```

ตัวเลือกทั้งหมดและเหตุผลเบื้องหลังค่า default อยู่ใน
[`docs/ALGORITHMS.md`](ALGORITHMS.md)

### 6.3 ใช้คอมโพสิเตอร์โดยไม่ต้องมีไดรเวอร์

ถ้ามีจอที่สองที่ความละเอียดสูงกว่าพาเนลหลัก:

```powershell
visual4k-host.exe --source \\.\DISPLAY2
```

> **ใช้ไม่ได้บนจอเดียวด้วย NVIDIA DSR** ถ้าตั้งเดสก์ท็อปเป็น 4K ด้วย DSR แล้วรัน
> คอมโพสิเตอร์ทับ หน้าต่างของคอมโพสิเตอร์เองก็อยู่บนเดสก์ท็อป 4K นั้น แล้วถูก DSR
> ย่อซ้ำอีกรอบ เป็นวงกลม จอเดียวไม่ลงไดรเวอร์ ให้ใช้ขั้น 0 แทน

### 6.4 คาดหวังอะไรได้บ้างเรื่องประสิทธิภาพ

เรนเดอร์ที่ 3840×2160 ใช้พิกเซลมากกว่า 1440p **2.25 เท่า** เฟรมเรตในเกมจะลดลง
ตามสัดส่วนคร่าว ๆ นี้ บวกกับ latency อีกอย่างน้อย 1 เฟรมจากตัวคอมโพสิเตอร์เอง

**ยังไม่ได้วัด latency จริง** จึงไม่ระบุตัวเลข ถ้าคุณวัดได้ ยินดีรับ

---

## ขั้น 7 — ถอนการติดตั้ง

```powershell
# PowerShell แบบ Administrator
.\tools\uninstall-driver.ps1
```

จะถอน device, ลบ driver package และลบ certificate ออกจาก trust store

**แล้วปิด test signing กลับด้วย:**

```powershell
bcdedit /set testsigning off
```

แล้ว reboot ข้อความ "Test Mode" จะหายไป

ถ้าหยุด BitLocker ไว้ตอนแรก เปิดกลับ:

```powershell
manage-bde -protectors -enable C:
```

---

## ตารางแก้ปัญหา

### ไดรเวอร์

| อาการ | สาเหตุและวิธีแก้ |
|---|---|
| Device Manager ขึ้น **Code 52** | ไดรเวอร์ยังไม่ได้เซ็น หรือ test signing ยังไม่เปิด/ยังไม่ reboot ตรวจด้วย `bcdedit /enum '{current}'` |
| Device Manager ขึ้น **Code 31 / Code 39** | catalog ไม่ตรงกับ binary รัน `install-driver.ps1` ใหม่ทั้งกระบวนการ |
| **จอเสมือนขึ้นแต่ไม่มีความละเอียดให้เลือก** | EDID ผิด รัน `.\build\Release\edid_selftest.exe` ถ้าผ่านหมด ปัญหาอยู่ที่ callback `EvtMonitorQueryTargetModes` |
| ไม่เห็นจอเสมือนเลย | `pnputil` staged ไดรเวอร์แล้วแต่ไม่มี device สร้างเองผ่าน Add legacy hardware (ขั้น 4) |
| `inf2cat failed` | ปัญหาใน `Visual4kDisplay.inf` ข้อความ error จะบอกว่า section ไหน |
| `bcdedit` สำเร็จแต่ testsigning ยังเป็น No | Secure Boot บล็อกอยู่ ต้องปิดใน UEFI |

### คอมโพสิเตอร์

| อาการ | สาเหตุและวิธีแก้ |
|---|---|
| **จอดำสนิท** | ไม่ได้คัดลอกโฟลเดอร์ `shaders\` ไปวางข้าง `.exe` — `build.ps1` ทำให้อัตโนมัติ ถ้า build เองต้องคัดลอกเอง |
| `could not duplicate the source display` | มีโปรแกรมจับภาพอื่น (OBS, Teams, Discord) ใช้ duplication ครบโควตาแล้ว ปิดก่อน |
| `renderer init failed` | shader คอมไพล์ไม่ผ่าน รันใน Debug build แล้วดู Output window ของ Visual Studio หรือ DebugView |
| **ภาพนุ่มกว่าที่คาด** | จอเสมือนไม่ได้ตั้งที่ 3840×2160 จริง ตรวจใน Settings อีกรอบ |
| **ภาพคมแต่เหมือนเลื่อนไปครึ่งพิกเซล** | บั๊กจริง รัน `pytest reference/tests -k half_pixel` แล้วแจ้งผล |
| **ไม่เห็นเคอร์เซอร์** | ตรวจว่าไม่ได้ใส่ `--no-cursor` ถ้าไม่ได้ใส่ก็เป็นบั๊ก |
| **เคอร์เซอร์อยู่ผิดตำแหน่ง** | น่าจะเป็นเรื่อง hot spot รายงานมาได้ |
| ปิดโปรแกรมไม่ได้ | Ctrl+Alt+F12 ถ้าโปรแกรมอื่นยึดปุ่มนี้ไว้ จะมี warning ตอนเริ่ม ใช้ Task Manager |

### ทั่วไป

| อาการ | วิธีแก้ |
|---|---|
| เข้า Windows ไม่ได้หลัง reboot | Safe Mode → `uninstall-driver.ps1` → `bcdedit /set testsigning off` |
| อยากย้อนทุกอย่าง | System Restore ไปที่จุดที่สร้างไว้ก่อนเริ่ม |

---

## สิ่งที่ยังไม่ได้ทดสอบ

พูดตรง ๆ ว่าโค้ดส่วนไหนผ่านอะไรมาแล้วบ้าง:

| ส่วน | สถานะ |
|---|---|
| แกน resample, เมตริก, CLI | ทดสอบแล้ว 77 tests |
| tap table C++ ↔ Python | ตรวจแล้ว ต่างกัน 3e-8 |
| EDID | ตรวจแล้ว checksum + timing 60.00 Hz |
| ตัวถอดรหัสเคอร์เซอร์ | ตรวจแล้วครบทั้งสามรูปแบบ รวมเคส invert |
| คอมโพสิเตอร์ D3D11 | build ผ่านบน MSVC + Windows SDK จริงทุก push |
| HLSL shaders | **ยังไม่เคยผ่านคอมไพเลอร์** — คอมไพล์ตอนรันบน GPU |
| ไดรเวอร์ IddCx | build + link ผ่านบน WDK จริงทุก push |
| ติดตั้งบนเครื่องจริง | **ยังไม่เคยมีใครทำสำเร็จ** — คุณคือคนแรก |

เจอ compile error หรือพฤติกรรมแปลก ๆ ส่งมาได้เลย
