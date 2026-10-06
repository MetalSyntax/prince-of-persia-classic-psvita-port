<!--
  GitHub Release body for v01.40. Paste the contents of this file into the release
  description at https://github.com/MetalSyntax/prince-of-persia-classic-psvita-port/releases/new
  (tag v01.40, title "v01.40 — Official PS Vita Trophies & Vendored vitaGL").
  Self-contained on purpose: absolute links only, no repo-relative paths, so it reads
  correctly on the Releases page. Keep it out of README.md.
-->

# v01.40 — Official PS Vita Trophies & Vendored vitaGL

**Full native PlayStation Vita trophy integration with Platinum, AI-enhanced assets, and vendored vitaGL eliminating black screen regressions.**

---

## 🇪🇸 Resumen en Español

Esta versión incorpora el sistema nativo oficial de **Trofeos de PlayStation Vita (`SceNpTrophy`)** y resuelve de raíz el problema de pantalla negra mediante la **vendorización de vitaGL**:

1. **Trofeos Nativos Oficiales con Platino (`POPC00001_00`)**:
   - Integración nativa completa con la base de datos de trofeos de PS Vita (`TROPHY.TRP`).
   - Los 17 logros originales del juego en Android (`AchievementManager`) ahora desbloquean sus respectivos trofeos de bronce, plata y oro.
   - Trofeo de **Platino exclusivo ("PRINCE OF PERSIA")** otorgado automáticamente al completar todos los logros del juego.
   - Arte de trofeos mejorado con **Inteligencia Artificial (Super-Resolución Real-ESRGAN x4)**: iconos corregidos en orientación, con nitidez ultra-alta y diseño limpio sin marcos artificiales postizos. Los PNGs originales en alta fidelidad se encuentran en `extras/trophy/icons/`.

2. **vitaGL Vendorizado (Solución de Pantalla Negra)**:
   - Se integró el árbol verificado de `vitaGL` de *Zenonia 3 / Zenonia 4* (`vendor/vitaGL`).
   - Compilado con `SOFTFP_ABI=1 NO_DEBUG=1 HAVE_SHADER_CACHE=1 NO_SPLASHSCREEN=1 HAVE_GLSL_UBOS=1 SAMPLERS_SPEEDHACK=1 DRAW_SPEEDHACK=2`.
   - Autodetección errónea de `system_app_mode` deshabilitada, eliminando por completo la regresión de pantalla negra en hardware real.

---

## 🇬🇧 English Release Notes

### Highlights

1. **Native PS Vita Trophy Support with Platinum (`POPC00001_00`)**:
   - Implemented full native Sony trophy system integration using `SceNpTrophy`.
   - Intercepted Cocos2d-x's `AchievementManager::UnLockAchievement` to seamlessly trigger PS Vita trophy unlocks in real time during gameplay.
   - All 17 official achievements from the original game are mapped to Bronze, Silver, and Gold trophies, translated and formatted.
   - Added an exclusive **Platinum Trophy ("PRINCE OF PERSIA")** unlocked upon obtaining all other 17 trophies.
   - Trophy artwork restored and boosted using **AI Super-Resolution (Real-ESRGAN x4 on GPU)**: correctly oriented upright, borderless, crystal clear. Source PNG icons are archived in `extras/trophy/icons/`.

2. **Vendored vitaGL (Black Screen Fix & Performance)**:
   - Vendored the battle-tested, high-performance `vitaGL` codebase from *Zenonia 3 / Zenonia 4* directly under `vendor/vitaGL/`.
   - Built with mandatory `SOFTFP_ABI=1` alongside speedhacks (`SAMPLERS_SPEEDHACK=1`, `DRAW_SPEEDHACK=2`, `HAVE_GLSL_UBOS=1`, `HAVE_SHADER_CACHE=1`).
   - Disabled faulty `system_app_mode` autodetection that was sending frames to a hidden shared framebuffer under `UNSAFE`/`NOASLR` + `kubridge` configurations, fixing the black screen bug on real consoles permanently.

---

### Trophy List (18 Trophies)

| ID | Grade | Title | Description |
|:---:|:---:|:---|:---|
| **000** | 🏆 **Platinum** | **PRINCE OF PERSIA** | Desbloquea todos los trofeos de Prince of Persia Classic. |
| **001** | 🥉 Bronze | **A SOLAS EN LA OSCURIDAD** | Rescata a la Princesa por primera vez. |
| **002** | 🥉 Bronze | **UN PASO ADELANTE** | Completa el nivel 1. |
| **003** | 🥉 Bronze | **SALUDABLE** | Encuentra un elixir de vida. |
| **004** | 🥉 Bronze | **EL HOMBRE DE LA HOJA** | Recoge la espada en el nivel 1. |
| **005** | 🥉 Bronze | **SIN ESPACIO PARA EL MIEDO** | Salta sobre un foso de pinchos en un combate con espada. |
| **006** | 🥉 Bronze | **SALTADOR DE FOSOS** | Salta sobre 2 fosos de pinchos seguidos en carrera. |
| **007** | 🥉 Bronze | **EL NACIMIENTO DE LA SOMBRA** | Mira en el espejo. |
| **008** | 🥈 Silver | **EL REFLEJO OSCURO** | Reúnete con el Príncipe Oscuro. |
| **009** | 🥇 Gold | **AMOR Y VENGANZA** | Derrota a Jaffar. |
| **010** | 🥈 Silver | **LA INMORTALIDAD** | Encuentra todos los elixires. |
| **011** | 🥉 Bronze | **RESCATE A TODA PRISA** | Supera un récord anterior de cualquier nivel en cualquier modo. |
| **012** | 🥇 Gold | **ARENAS DEL DESTINO** | Completa el juego en modo Contrarreloj. |
| **013** | 🥇 Gold | **EL VERDADERO PRÍNCIPE** | Completa el juego en modo Supervivencia. |
| **014** | 🥈 Silver | **IMPARABLE** | Completa el nivel sin perder salud. |
| **015** | 🥈 Silver | **EL PACIFICADOR** | Completa el nivel sin matar a un solo guardia. |
| **016** | 🥉 Bronze | **IRA DESATADA** | Mata al primer enemigo. |
| **017** | 🥉 Bronze | **UN PEDAZO DE VIDA** | Mata a un guardia con la rebanadora, con un pincho o haciéndolo caer. |

---

### Installation & Update Instructions

1. Download **`popclassic.vpk`** from this release.
2. Install via **VitaShell** on your PS Vita (HENkaku / Ensō / h-encore).
3. If updating from v01.20+: simply install over the previous version! The `TITLEID` remains `POPC00001`, and all save data is preserved.
4. Ensure your game data folder is present at `ux0:data/popclassic/`.
