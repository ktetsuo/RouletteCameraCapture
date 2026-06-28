# RouletteCameraCapture

## 環境構築

### 前提
- Windows 11
- Visual Studio 2026（C++ デスクトップ開発）
- Qt 6.11.1（`msvc2022_64`）
- Qt Visual Studio Tools（Visual Studio 拡張機能）

### セットアップ手順
1. Visual Studio Installer で **C++ によるデスクトップ開発** をインストールします。
2. Qt Online Installer で **Qt 6.11.1 / msvc2022_64** をインストールします。
3. Visual Studio に **Qt Visual Studio Tools** をインストールします。
4. Visual Studio で `Qt VS Tools` の Qt バージョン設定を開き、以下を追加します。
   - Name: `6.11.1_msvc2022_64`
   - Path: インストールした Qt 6.11.1（msvc2022_64）のパス
5. `RouletteCameraCapture.vcxproj`（または `.sln`）を Visual Studio で開きます。
6. 構成を `Debug|x64` または `Release|x64` にしてビルドします。

### 起動
- Visual Studio から `RouletteCameraCapture` をスタートアップ プロジェクトにして実行します。
