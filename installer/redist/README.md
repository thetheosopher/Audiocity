# VC++ Redistributable payload

Place `VC_redist.x64.exe` in this folder for WiX bootstrapper builds.

Expected path:
- `installer/redist/VC_redist.x64.exe`

The bootstrapper (`AudiocitySetup.exe`) chains:
1. VC++ Redistributable x64 installer
2. `AudiocityInstaller.msi`

If you keep `VC_redist.x64.exe` elsewhere, pass `/p:VcRedistSourcePath=<full-path>` to `msbuild installer/AudiocityBootstrapper.wixproj`.
