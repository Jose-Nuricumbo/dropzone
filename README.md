# Dropzone 🚀

A minimalist, ultralight floating document converter and drop target designed in GTK 4.

---

## 📋 What it does
**Dropzone** provides a sleek, GPU-accelerated floating orb interface that stays on your desktop. It allows you to drag and drop files (such as PDF, Word, Excel, PowerPoint, and Text documents) to instantly convert them into multiple formats with a smooth, modern user experience.

---

## ⚙️ Requirements & What to Keep in Mind
* **GTK Version:** Requires **GTK 4.14** or higher.
* **Dependencies:** `libc6`, `libgtk-4-1 (>= 4.14)`, `poppler-utils`.
* **Conversion Backends:** To handle document conversions seamlessly, ensure you have **LibreOffice** (`libreoffice` / `soffice`) or **OnlyOffice** (`x2t`) installed on your system.

---

## 📦 How to Install

You can install Dropzone easily on Debian/Ubuntu-based distributions using the pre-compiled `.deb` package available in the [Releases](https://github.com/Jose-Nuricumbo/dropzone/releases) section:

1. Download `dropzone_1.0.0_amd64.deb` from the latest release.
2. Open your terminal in the download folder and run:
   ```bash
   sudo apt install ./dropzone_1.0.0_amd64.deb
