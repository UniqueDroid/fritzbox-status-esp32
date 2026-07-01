<!-- Use this file to provide workspace-specific custom instructions to Copilot. For more details, visit https://code.visualstudio.com/docs/copilot/copilot-customization#_use-a-githubcopilotinstructionsmd-file -->
- [x] Verify that the copilot-instructions.md file in the .github directory is created.
Summary: Datei unter .github erstellt.

- [x] Clarify Project Requirements
	Summary: ESP32/LilyGO T-Display S3, pfSense REST API, Captive Portal, NerdMiner-Style UI.
	<!-- Ask for project type, language, and frameworks if not specified. Skip if already provided. -->

- [x] Scaffold the Project
	Summary: PlatformIO-Projekt mit platformio.ini, src/main.cpp, README.md und .gitignore erstellt.
	<!--
	Ensure that the previous step has been marked as completed.
	Call project setup tool with projectType parameter.
	Run scaffolding command to create project files and folders.
	Use '.' as the working directory.
	If no appropriate projectType is available, search documentation using available tools.
	Otherwise, create the project structure manually using available file creation tools.
	-->

- [x] Customize the Project
	Summary: WiFiManager Captive Portal, Preferences-Storage, pfSense API Polling und TFT Dashboard integriert.
	<!--
	Verify that all previous steps have been completed successfully and you have marked the step as completed.
	Develop a plan to modify codebase according to user requirements.
	Apply modifications using appropriate tools and user-provided references.
	Skip this step for "Hello World" projects.
	-->

- [x] Install Required Extensions
	Summary: Keine zusaetzlichen Extensions explizit erforderlich.
	<!-- ONLY install extensions mentioned in the project setup information. Skip this step otherwise and mark as completed. -->

- [x] Compile the Project
	Summary: Build mit lokaler .venv und PlatformIO erfolgreich (pio run).

- [x] Create and Run Task
	Summary: VS Code Task "Build ESP32 Firmware" erstellt und erfolgreich ausgefuehrt.

- [x] Launch the Project
	Summary: Firmware erfolgreich auf /dev/ttyACM0 geflasht und gestartet.

- [x] Ensure Documentation is Complete
	Summary: README und copilot-instructions vorhanden und aktualisiert; HTML-Kommentare entfernt.

- Work through each checklist item systematically.
- Keep communication concise and focused.
- Follow development best practices.
