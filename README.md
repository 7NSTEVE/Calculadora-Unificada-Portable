# Calculadora Unificada & CuadernU (Windows)

Aplicación nativa para Windows que integra en una distribución portable (`CalculadoraUnificada.exe` Win32 + `CuadernU.exe` x64 Direct2D, con alias de compatibilidad `DocumentStudio.exe`) las **cuatro calculadoras científicas y graficadoras CASIO** sobre sus motores y firmwares originales (`fx-991CW ClassWiz II`, `fx-570LA X / 991LA X`, `fx-570VN PLUS / ES PLUS` y `fx-CG50 PRIZM`) junto con **CuadernU** (Cuaderno Digital, Lienzo Infinito, Editor PDF, Laboratorio STEM, Migración Multiformato y Módulos Académicos), con interfaz bajo **Apple HIG + Liquid Glass 2.0** en modos **Claro**, **Oscuro** y **Alto Contraste (WCAG AAA)**, tipografía matemática natural en **2D** con teclas gráficas CASIO, modo compacto vertical con cajón lateral expandible de secuencia, capturas automáticas pre-entrada (`App · LCD`), anotador gráfico a mano alzada y **Visor Nativo de PDF sin dependencias externas** sobre **16 manuales oficiales** (Hardware y Software, Español e Inglés).

---

## Vista previa de capturas (`docs/screenshots/`)

| Apple HIG + Liquid Glass 2.0 Claro (4 calculadoras activas) | Modo Oscuro (`fx-991CW` Solo + Dock Liquid Glass 2.0) |
| :---: | :---: |
| ![Comparar 4 calculadoras Liquid Glass Claro](docs/screenshots/lg_calc_compare_all4_light.png) | ![fx-991CW Modo Oscuro Liquid Glass](docs/screenshots/lg_calc_solo_cw_dark.png) |

| CuadernU — Lienzo STEM 2D + MNA + 3D CVD (Claro) | CuadernU — Modo Oscuro + Inspector STEM |
| :---: | :---: |
| ![CuadernU STEM Liquid Glass Claro](docs/screenshots/lg_studio_stem_light.png) | ![CuadernU Oscuro con Inspector STEM](docs/screenshots/lg_studio_stem_dark_popover.png) |

| Exportador de Fórmulas 2D (Oscuro) | Modo Compacto Vertical (Oscuro) | Visor Nativo PDF (`Windows.Data.Pdf`) | CuadernU — Módulos Académicos |
| :---: | :---: | :---: | :---: |
| ![Exportador de Fórmulas 2D Modo Oscuro](docs/screenshots/lg_calc_exporter_dark.png) | ![Modo Compacto Liquid Glass Oscuro](docs/screenshots/lg_calc_compact_dark.png) | ![Visor Nativo PDF 16 manuales](docs/screenshots/lg_calc_pdf_light.png) | ![CuadernU Inspector Académico](docs/screenshots/lg_studio_academic_light.png) |

---

## Características Principales

### 1. Cuatro Calculadoras Oficiales Integrados (`CalculadoraUnificada.exe`)
- **CASIO fx-991CW ClassWiz II (Negra)**:
  - Host nativo (`fx991cw_host.cpp`) integrando `runtime/fx991cw/emulator/casioemu.exe` (ventana `SDL_app` embebida) con overlay transparente interactivo para resaltado de hover/tecla activa (`WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_NOACTIVATE`), registro completo de pulsaciones en Historial y capturas automáticas previas al confirmar en menús (`Pre-OK` / `App · LCD`).
- **CASIO fx-570LA X / fx-991LA X ClassWiz (Blanca)**:
  - Núcleo `nX-U8` (`third_party/CalcEmuCore`) + ROM `roms/fx991lax.rom` embebida en recursos PE (`3001 RCDATA`).
  - Indicadores hardware Row-0 propios (`lcd_indicators.h` sobre malla nativa `192 x 8` con `COLORONCOLOR`: `[S]`, `[A]`, `M`, `STO`, `[Math]`, `[D]`/`[R]`/`[G]`, `FIX`, `SCI`, `E`, `i`, flechas de desplazamiento).
- **CASIO fx-570VN PLUS / fx-570ES PLUS Natural-V.P.A.M. (Gris)**:
  - Núcleo `nX-U8` + ROM `roms/fx570vnplus.rom` embebida en recursos PE (`3002 RCDATA`) + indicadores Row-0 propios en malla `288 x 12` (`lcd_indicators.h`).
- **CASIO fx-CG50 PRIZM (Graficadora Color)**:
  - Motor oficial CASIO 32-bit (`cg50_backend.cpp`) vía las DLL de `runtime/` (`GCalAppIF.dll`, `DLSim32E.dll`, `CPU73050.dll`, `LCDR61524A.dll`, `Key1616SwA.dll`, `CASIO GCSim.dll`, `ActivationFxMb.dll` y `fxCG50.dll`) + redistribuibles locales (`mfc100.dll`, `mfc100u.dll`, `msvcp100.dll`, `msvcr100.dll`) para portabilidad en equipos limpios.

### 2. Interfaz Apple HIG + Liquid Glass 2.0, Renderizado Matemático 2D y Modo Compacto
- **Sistema Visual Apple HIG & Liquid Glass 2.0 (`ui_theme.h`, `ui_glass/`)**:
  - Modos de apariencia **Claro**, **Oscuro** y **Alto Contraste (WCAG AAA)** con barra de título DWM adaptativa y bordes redondeados nativos en Windows 11.
  - Cápsulas flotantes ópticamente refractadas con desenfoque *Dual Kawase*, bisel de refracción espectral RGB (ley de Snell), reflexión especular de Fresnel, bordes concéntricos (`r_interno = máx(r_externo - padding, r_mín)`), control de opacidad (*Diseño*, *Equilibrado*, *Esmerilado Opaco*) y opción *Reducir Transparencia*.
  - Orbes vítreos de estado y tinta, chasis CASIO recortados por canal alfa con sombra de contacto sutil e iconografía vectorial GDI+ AntiAlias.
- **Motor Visual Subconjunto LaTeX 2D (`latex_math_renderer.h`)**: renderiza visualmente fracciones (`\frac`), raíces (`\sqrt`), integrales (`\int`), sumatorias (`\sum`), límites (`\lim`), potencias, subíndices, paréntesis adaptativos y letras griegas en Historial, Inspector, Centro de Estudio y Exportador sin exponer tokens `$...$` crudos.
- **Botones rápidos por calculadora (`main.cpp`)**: accesos vectoriales junto a cada chasis en vista Solo y Comparar para entrar/salir del modo ventana compacta (`[|O|]`) y fijar *Always on Top* (candado).
- **Modo Ventana Compacta Vertical + Cajón Expandible (`Ctrl+M` / `Ctrl+U`)**:
  - Muestra una única calculadora (`fx-991CW`, `fx-570/991LA X`, `fx-570VN PLUS` o `fx-CG50 PRIZM`) con pestaña lateral derecha expandible (**Secuencia**) para ver u ocultar la tira de pasos en vivo, botón de limpieza rápida (basurerito), capturas `App · LCD` pre-entrada, resaltado azul direccional en el D-pad/`REPLAY` de los 4 modelos y acceso directo a **Anotar / Compartir**.
- **Pop-up Display (`Ctrl+P`, `popup_display.h`)**:
  - Ventana flotante sincronizada en tiempo real (25 FPS) sobre dimensiones lógicas exactas del LCD con captura PNG/portapapeles, cuadrícula y escalado (`x1 / x2 / x3`).
- **Portapapeles Inteligente (`Ctrl+,`) e Historial (`F2`, `key_history_view.h`)**:
  - Copia persistente configurable en `config_unificada.ini` (**Solo Imagen** `CF_BITMAP`, **Solo Texto + LaTeX**, o **Ambas Simultáneamente**), miniaturas reales de botones con dirección D-pad y reproductor paso a paso (`|<`, `<`, `Reprod`, `>`, `>|`, exportación `./guias/reproductor_pasos/paso_XXX.png`).
- **Exportador de Guías y Anotador Mano Alzada (`Ctrl+E`, `settings_and_exporter.h`)**:
  - Selección de rango (`Desde #A` a `Hasta #B`), formatos predeterminados (`Explicada`, `Rápida`, `Práctica`, `Presentación`), notas editables con LaTeX 2D, herramienta de dibujo a mano alzada (`Lápiz`), texto/notas y formas vectoriales (`Texto`, `Flecha`, `Círculo`, `Marco`, `#N`), copia al portapapeles y exportación PNG/BMP.
- **Deshacer Coherente (`Ctrl+S` / `Ctrl+Z`)**:
  - Captura y restaura de forma atómica el estado binario de RAM junto con `KeyLog`, galería y ventanas.

### 3. Visor Nativo de PDF sin Dependencias, Referencia Offline y 16 Manuales Oficiales
- **Visor Nativo de PDF (`pdf_viewer.h`)**: implementado directamente en Win32 usando `Windows.Data.Pdf` (WinRT COM cargado dinámicamente en un hilo MTA dedicado, sin librerías de terceros ni navegador embebido), con paginación, zoom, barra de temas/índice y diálogo `Abrir...` para cualquier PDF externo.
- **Catálogo Determinista Offline (`knowledge_catalog.h`, `smart_help_view.h`, `learning_studio.h`, `manual_catalog.h`)**:
  - Capas `SHIFT`/`ALPHA` (`Ctrl+Shift+L`), Explorador Interactivo (`Ctrl+Shift+K`) y Centro de Estudio (`Ctrl+Shift+E`, pestaña `Manuales PDF (16)`) respaldados por `catalog/casio_knowledge_catalog.json` e índice de búsqueda sobre `manuales/indice_paginas_pdf.tsv`.

| Calculadora | Manuales Oficiales Incluidos en `manuales/` (`16` PDFs) | Tipo / Idioma |
| :--- | :--- | :--- |
| **Negra (`fx-991CW / fx-570CW`)** | `fx-570CW_991CW_ES.pdf` (158 pág.), `fx-570CW_991CW_EN.pdf` (151 pp.) | Usuario / HW & SW (ES / EN) |
| **Blanca (`fx-570LA X / 991LA X / EX`)** | `fx-570_991LA_X_ES.pdf` (49 pág.), `fx-570_991EX_ES.pdf` (48 pág.), `fx-570_991EX_EN.pdf` (46 pp.) | Hardware / Usuario (ES / EN) |
| **Blanca / Negra (Software Emulador)** | `ClassWiz_Emulator_Software_Users_Guide_ES.pdf` (88 pág.), `CASIO_Activation_Guide_ES.pdf` (112 pág.) | Software Emulador y Licencia (ES) |
| **Gris (`fx-570VN PLUS / ES PLUS`)** | `fx-570_991ES_PLUS_ES.pdf` (48 pág.), `fx-570_991ES_PLUS_EN.pdf` (46 pp.), `fx-570VN_PLUS_Hardware_Users_Guide.pdf` (62 pp.) | Hardware / Usuario (ES / EN) |
| **Graficadora (`fx-CG50 PRIZM`)** | `fx-CG50_Soft_v370_ES.pdf` (678 pág.), `fx-CG50_Soft_v370_EN.pdf` (677 pp.), `fx-CG50_Hard_ES.pdf` (17 pág.), `fx-CG50_Hard_EN.pdf` (17 pp.) | Software OS v3.70 y Hardware (ES / EN) |
| **Graficadora (Software Emulador)** | `fx-CG_Manager_PLUS_Software_Users_Guide_EN.pdf` (45 pp.), `fx-CG_Manager_PLUS_Activation_Guide.pdf` (270 pág.) | Software `fx-CG Manager PLUS` y Activación |

---

## Ejecución Portable y Línea de Comandos

```bat
iniciar.bat
```

Opciones soportadas por `CalculadoraUnificada.exe`:
- `CalculadoraUnificada.exe` — Vista principal unificada.
- `CalculadoraUnificada.exe --compact` — Modo ventana compacta (`fx-570/991LA X`).
- `CalculadoraUnificada.exe --compact-gray` — Modo ventana compacta (`fx-570VN PLUS`).
- `CalculadoraUnificada.exe --compact-cg50` — Modo ventana compacta (`fx-CG50 PRIZM`).
- `CalculadoraUnificada.exe --compact --popup` — Modo ventana compacta junto con *Pop-up Display*.
- `CalculadoraUnificada.exe --help-search` — Ayuda Inteligente y buscador de manuales PDF.

---

## Compilación y Verificación (`Win32 / MSVC`)

```bat
compilar.bat
```

O con CMake en árbol de verificación limpia (`Win32`, CRT estático `/MT`):
```powershell
cmake -S . -B build-verify -A Win32
cmake --build build-verify --config Release --clean-first
ctest --test-dir build-verify -C Release --output-on-failure --verbose
```

Suites automatizadas registradas en `CMakeLists.txt`:
- `Cg50BackendContract` (`tests/cg50_backend_contract_test.cpp`)
- `UiCoreModules` (`tests/ui_core_modules_test.cpp` — 11 módulos incluyendo `TestNativePdfViewerAll16Manuals`)

---

## CuadernU (Cuaderno Digital, Lienzo Infinito, Editor PDF, Migración Multiformato y Laboratorio STEM)

El subsistema unificado **CuadernU** (`doc_studio/` en Windows `x64` Direct2D, incorporando el núcleo vectorial `tinta_core/` y los módulos universitarios `academic/`) proporciona:
- **Interfaz Apple HIG + Liquid Glass 2.0**: barra superior con cápsulas flotantes refractadas, barra lateral *edge-to-edge* con índice PDF (`/Outlines`), miniaturas e iconos vectoriales semánticos, popover interactivo de Apariencia (*Claro / Oscuro / Alto Contraste*, deslizador continuo de opacidad del vidrio y *Reducir Transparencia*), múltiples pestañas, vista dividida lado a lado y ventana de proyector dedicada.
- **Manipulación Directa y Motor de Tinta Profesional**: arrastre, redimensionado con 8 manejadores y rotación en lienzo, selección múltiple con lazo, agrupación/desagrupación, edición in-situ con doble clic, borrador de precisión por píxel o trazo completo, 4 estilos de lápiz (*Bolígrafo*, *Pluma Fuente*, *Lápiz*, *Pincel*), rechazo estricto de palma, soporte de borrador invertido y botón lateral del lápiz digital, ajuste a cuadrícula, herramientas geométricas (*Regla*, *Transportador*, *Compás*), reconocimiento offline de escritura a texto/ecuación (HTR) y 11 plantillas técnicas de papel (*Isométrico 30°*, *Log-Log*, *Polar*, *Carta de Smith*, *Cornell*, *Pentagrama*, *Milimetrado*, etc.).
- **Motor de Interoperabilidad y Migración Multiformato de Alta Fidelidad**: importación nativa con análisis previo de compatibilidad en 3 niveles (**Visual**, **Estructural** y **Editable**) para **Goodnotes 6 (`.goodnotes`)**, **Notability (`.note`)**, **Microsoft OneNote (`.one` / `.onepkg`)**, **CuadernU Clásico (`.tinta`)**, **Microsoft Word (`.docx`)**, **Microsoft PowerPoint (`.pptx`)** y **PDF (`.pdf`)**, preservando trazos manuscritos como objetos vectoriales editables, tablas, ecuaciones, imágenes, audio vinculado y jerarquía de secciones.
- **Audio Sincronizado e Ink Replay**: grabación continua no bloqueante desde micrófono (`WinMM waveIn`), reproducción real por altavoces (`waveOut`) sincronizada con los trazos, marcadores, búsqueda por marca de tiempo, segmentación offline y exportación de tutorial interactivo HTML5.
- **Laboratorio STEM, Seguridad y Puente CASIO en Vivo**: motor matemático dimensional (`#varsub` / `#nosub` / `#novar`), matrices complejas y valores propios, EDO RK4, tablas reactivas con fórmulas, superficies `3D` rotables con ratón, simulador de circuitos **MNA** (`DC`, `AC`, diodos, osciloscopio transitorio, Laplace simbólico `H(s)`, reconocedor de circuitos a mano alzada y mapas de Karnaugh), verificación de pasos (*Check My Work*), guía socrática (*Teach Me*), comparador de 4 modelos CASIO, envío en vivo sin red desde `CalculadoraUnificada.exe` a `CuadernU.exe`, cifrado `AES-256-GCM`, difusión LAN con QR y historial de versiones sin pérdida.
- **Módulos Académicos Universitarios (`academic/`)**: conector de solo lectura para TEC Digital y optimizador de horarios **HorariosTEC** sin choques.
- Guía completa de funciones y sintaxis: [docs/MANUAL_DOCUMENT_STUDIO_INGENIERIA.md](docs/MANUAL_DOCUMENT_STUDIO_INGENIERIA.md).

