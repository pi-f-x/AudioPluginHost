# AudioPluginHost

Kompaktes Host-Projekt basierend auf JUCE (C++17). Dieses Repository enthält einen einfachen Audio/Plugin-Host mit interner Plugin-Sammlung und graphischer Oberfläche.

## Kurzbeschreibung
- Host für Audio-Plugins (VST/VST3/AU/Interne).
- Graph-basierte Plugin-Verknüpfung, MIDI- und Audio-Routing.
- Beispiel- und Demo-Plugins eingebettet (sofern vorhanden).

## Voraussetzungen
- Visual Studio 2022 (Windows) oder ein entsprechendes C++17-fähiges Toolchain.
- JUCE (im Projekt eingebunden / als Submodule oder Produktions-Build).
- Optional: Git / GitHub CLI (`gh`) für Remote-Publishing.

## Build (Windows)
1. Solution öffnen: `AudioPluginHost.sln` in Visual Studio 2022.
2. Konfiguration wählen: z. B. __Debug x64__.
3. Projekt bauen: __Build > Build Solution__.

Wenn Header wie `BinaryData.h` oder example-Header fehlen, siehe Build-Logs — ggf. Beispiele/Hinweise nachimportieren oder die bedingten Includes in `Source/Plugins/InternalPlugins.cpp` aktivieren.

## Schnellstart (CLI)
