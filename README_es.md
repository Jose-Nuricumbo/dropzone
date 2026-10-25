# Dropzone 🚀

Un conversor de documentos flotante minimalista y ultraligero diseñado en GTK 4.

---

## 📋 Qué hace
**Dropzone** ofrece una elegante interfaz de orbe flotante con aceleración por GPU que se integra perfectamente en tu escritorio. Te permite arrastrar y soltar archivos (como PDF, Word, Excel, PowerPoint y documentos de texto) para convertirlos instantáneamente a múltiples formatos con una experiencia de usuario fluida y moderna.

---

## ⚙️ Requisitos y qué se debe tomar en cuenta
* **Versión de GTK:** Requiere **GTK 4.14** o superior.
* **Dependencias:** `libc6`, `libgtk-4-1 (>= 4.14)`, `poppler-utils`.
* **Motores de conversión:** Para procesar las conversiones de documentos de oficina de forma óptima, asegúrate de tener instalado **LibreOffice** (`libreoffice` / `soffice`) u **OnlyOffice** (`x2t`) en tu sistema.

---

## 📦 Cómo instalarlo

Puedes instalar Dropzone fácilmente en distribuciones basadas en Debian/Ubuntu usando el paquete `.deb` precompilado disponible en la sección de [Releases](https://github.com/Jose-Nuricumbo/dropzone/releases):

1. Descarga el archivo `dropzone_1.0.0_amd64.deb` desde el último lanzamiento.
2. Abre tu terminal en la carpeta donde lo descargaste y ejecuta:
   ```bash
   sudo apt install ./dropzone_1.0.0_amd64.deb
