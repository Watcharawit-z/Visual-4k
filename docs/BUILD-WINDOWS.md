# การ build และติดตั้งบน Windows

เอกสารนี้ครอบคลุมสองส่วนที่แยกกันชัดเจน: **คอมโพสิเตอร์** (โปรแกรมธรรมดา ติดตั้งง่าย)
และ **ไดรเวอร์** (ต้องเซ็นดิจิทัล หรือเปิดโหมดทดสอบ)

> **สถานะ:** โค้ดฝั่ง Windows ทั้งหมดเขียนขึ้นบน Linux ที่ไม่มี Windows SDK/WDK
> จึงยังไม่เคยผ่านคอมไพเลอร์ ให้เผื่อเวลาแก้ compile error รอบแรกไว้ด้วย
> ส่วนตรรกะเชิงตัวเลข (tap table, EDID) ทดสอบผ่านแล้วและไม่ขึ้นกับ Windows

---

## 1. คอมโพสิเตอร์ (visual4k-host)

### ต้องมี
- Visual Studio 2022 พร้อม workload "Desktop development with C++"
- CMake 3.20 ขึ้นไป
- Windows 10 SDK

### build

```powershell
cmake -B build -S . -A x64
cmake --build build --config Release
```

ได้ `build\host\visual4k-host\Release\visual4k-host.exe` พร้อมโฟลเดอร์ `shaders\`
ที่ถูกคัดลอกไปวางข้าง ๆ ให้อัตโนมัติ — shader คอมไพล์ตอนรัน จึงแก้แล้วเห็นผลทันที
โดยไม่ต้อง build ใหม่

### ทดสอบก่อนติดตั้งไดรเวอร์

คอมโพสิเตอร์ทำงานกับจออะไรก็ได้ที่ความละเอียดสูงกว่าพาเนล ถ้าคุณมีจอที่สองอยู่แล้ว
หรือใช้ NVIDIA DSR / AMD VSR เพื่อสร้างโหมดความละเอียดสูงได้ ก็ทดสอบได้เลย:

```powershell
.\visual4k-host.exe --list-displays
.\visual4k-host.exe --source \\.\DISPLAY2
```

---

## 2. ไดรเวอร์ (Visual4kDisplay)

### ต้องมี
- [Windows Driver Kit (WDK)](https://learn.microsoft.com/windows-hardware/drivers/download-the-wdk)
  เวอร์ชันที่ตรงกับ Visual Studio ของคุณ
- Windows 10 build 16299 (1709) ขึ้นไป — เป็นรุ่นแรกที่มี IddCx

### build

WDK ติดตั้ง project template และ MSBuild targets ให้ ถ้าต้องการสร้าง `.vcxproj`
ให้ใช้ template **"User Mode Driver (UMDF V2)"** แล้วเพิ่มไฟล์เหล่านี้:

```
driver/Visual4kDisplay/Driver.h
driver/Visual4kDisplay/Driver.cpp
driver/Visual4kDisplay/Edid.h
driver/Visual4kDisplay/Edid.cpp
driver/Visual4kDisplay/SwapChainProcessor.cpp
driver/Visual4kDisplay/Visual4kDisplay.inf
```

ตั้งค่าที่ต้องแก้จากค่า default ของ template:
- **Configuration Properties → Driver Settings → Target Platform**: `Universal`
- **Linker → Input → Additional Dependencies**: เพิ่ม
  `IddCx.lib`, `d3d11.lib`, `dxgi.lib`, `avrt.lib`
- **C/C++ → Language → C++ Language Standard**: `ISO C++17` (โค้ดใช้
  `std::unique_ptr` และ lambda ใน WDF callback)

### เซ็นไดรเวอร์

Windows จะไม่โหลดไดรเวอร์ที่ไม่ได้เซ็น มีสองทาง:

**ทางที่ 1 — โหมดทดสอบ (สำหรับเครื่องตัวเอง)**

```powershell
# ต้องรันใน PowerShell แบบ Administrator
bcdedit /set testsigning on
# แล้ว reboot
```

หลัง reboot จะมีข้อความ "Test Mode" ที่มุมขวาล่างของหน้าจอ สร้าง certificate
สำหรับทดสอบและเซ็น:

```powershell
$cert = New-SelfSignedCertificate -Type CodeSigningCert `
    -Subject "CN=Visual-4k Test" -CertStoreLocation Cert:\CurrentUser\My
Export-Certificate -Cert $cert -FilePath visual4k-test.cer
Import-Certificate -FilePath visual4k-test.cer `
    -CertStoreLocation Cert:\LocalMachine\Root
Import-Certificate -FilePath visual4k-test.cer `
    -CertStoreLocation Cert:\LocalMachine\TrustedPublisher
```

> **ข้อควรรู้:** โหมดทดสอบลดระดับความปลอดภัยของเครื่องลง เพราะยอมให้โหลด
> ไดรเวอร์ที่ไม่ได้ผ่านการรับรอง ปิดกลับด้วย `bcdedit /set testsigning off`
> เมื่อเลิกใช้ และอย่าเปิดทิ้งไว้บนเครื่องที่ใช้งานจริง

**ทางที่ 2 — เซ็นจริง (สำหรับแจกจ่าย)**

ต้องมี EV code-signing certificate และส่งไดรเวอร์ผ่าน
[Windows Hardware Dev Center](https://partner.microsoft.com/dashboard/hardware)
เพื่อรับ attestation signature จาก Microsoft ค่าใช้จ่ายและขั้นตอนมากพอสมควร
สมเหตุสมผลก็ต่อเมื่อจะแจกจ่ายให้คนอื่น

### ติดตั้ง

```powershell
# ด้วย devcon (มากับ WDK)
devcon install Visual4kDisplay.inf Root\Visual4kDisplay

# หรือด้วย pnputil
pnputil /add-driver Visual4kDisplay.inf /install
```

ถ้าใช้ `pnputil` ต้องสร้าง device เองผ่าน Device Manager →
Action → Add legacy hardware → เลือกจากรายการ → Display adapters →
Have Disk → ชี้ไปที่ INF

### ถอนการติดตั้ง

```powershell
devcon remove Root\Visual4kDisplay
pnputil /delete-driver oem<N>.inf /uninstall     # หา <N> ด้วย pnputil /enum-drivers
```

---

## 3. ใช้งาน

หลังติดตั้งไดรเวอร์ จะเห็นจอใหม่ชื่อ "Visual-4k Virtual Display" ใน
Settings → System → Display

1. ตั้งจอเสมือนให้เป็น **จอหลัก** และตั้งความละเอียดเป็น 3840×2160
2. ตั้งจอจริงให้ **extend** (อย่าตั้งเป็น mirror — คอมโพสิเตอร์จะวาดทับเอง)
3. รัน `visual4k-host.exe` — หน้าต่างจะเปิดเต็มจอจริงและแสดงผลที่ resolve แล้ว

หน้าต่างเป็น borderless topmost ไม่ใช่ exclusive fullscreen โดยตั้งใจ:
exclusive fullscreen จะยึดพาเนลไปจาก DWM ซึ่งจะพังเดสก์ท็อปที่เรากำลังจะแสดงพอดี

---

## แก้ปัญหา

| อาการ | สาเหตุที่พบบ่อย |
|---|---|
| จอเสมือนขึ้นแต่ไม่มีความละเอียดให้เลือก | EDID ผิด — รัน `build/edid_selftest` เพื่อตรวจ |
| ไดรเวอร์ไม่โหลด, Code 52 | ยังไม่ได้เซ็น หรือยังไม่ได้เปิด testsigning แล้ว reboot |
| `visual4k-host` บอกว่า duplicate ไม่ได้ | มีโปรแกรมจับภาพอื่นใช้ duplication อยู่ครบโควตาแล้ว |
| จอดำ | ตรวจว่าคัดลอกโฟลเดอร์ `shaders/` ไปข้าง `.exe` แล้ว |
| ภาพนุ่มกว่าที่คาด | ตรวจว่าจอเสมือนตั้งไว้ที่ 3840×2160 จริง ไม่ใช่ 2560×1440 |
| ภาพคมแต่เหมือนเลื่อนไปครึ่งพิกเซล | เป็นบั๊กจริง — รัน `pytest reference/tests -k half_pixel` |
