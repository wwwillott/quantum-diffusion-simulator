#include "raylib.h"
#include "SimConfig.h"
#include "Simulator1D.h"
#include "Simulator2D.h"
#include "DataParser.h"

enum class AppState { DIMENSION_SELECT, MAIN_MENU, SETUP_SANDBOX, SETUP_2D_SANDBOX, SETUP_FIREBREAK, SETUP_SEARCH, SIMULATION, VIRUS_DATASET_SELECT, VIRUS_CONFIG_SETUP };

int main() {
    const int screenWidth = 1000;
    const int screenHeight = 650;

    SimConfig mySettings;
    mySettings.mode = SimMode::QUANTUM; 
    mySettings.resolution = 400; 
    mySettings.diffusion_rate = 0.45f;
    mySettings.initial_coin = CoinState::SYMMETRIC; 
    mySettings.init_state_2d = InitialState2D::ALTERNATING_PHASE;
    mySettings.wave_start_pos = 200;
    mySettings.target_position = -1; 
    mySettings.num_barriers = 0; 
    for(int i=0; i<4; i++) mySettings.barrier_positions[i] = 250;

    SetConfigFlags(FLAG_MSAA_4X_HINT);
    InitWindow(screenWidth, screenHeight, "Quantum Walk Virtual Machine");
    SetTargetFPS(20); 

    Simulator1D sim1D(mySettings); 
    Simulator2D sim2D(mySettings); 

    bool isPaused = false;
    bool showInfoOverlay = false;
    bool is2DMode = false;

    int dimSelection = 0;
    int menuSelection = 0;
    int sandboxSelection = 0;

    AppState currentState = AppState::DIMENSION_SELECT;
    
    // Firebreak Setup Variables
    int setupSelection = 0;
    int fb_start = 100;
    int fb_target = 300;
    int fb_num_barriers = 1;
    int fb_barriers[4] = {200, 220, 240, 260};

    // Search Setup Variables
    int search_coord = 200;

    // Virus Simulation Variables
    int virusMenuSelection = 0;
    int virusConfigSelection = 0;
    const char* selectedDataset = "NONE";
    int timeStepMode = 1; 
    int mapPresetMode = 0; 
    int landscapeMode = 0; 

    while (!WindowShouldClose()) {
        
        // ==========================================
        // INPUT & LOGIC PHASE
        // ==========================================
        if (currentState == AppState::DIMENSION_SELECT) {
            if (IsKeyPressed(KEY_DOWN) || IsKeyPressed(KEY_UP)) dimSelection = (dimSelection + 1) % 2;
            
            if (IsKeyPressed(KEY_ENTER)) {
                is2DMode = (dimSelection == 1); 
                
                if (is2DMode) {
                    mySettings.resolution = 100;
                    mySettings.steps_per_frame = 5;
                } else {
                    mySettings.resolution = 400;
                    mySettings.steps_per_frame = 1;
                }
                
                mySettings.boundary_condition = BoundaryType::ABSORBING;

                currentState = AppState::MAIN_MENU;
                menuSelection = 0; 
            }
        }
        else if (currentState == AppState::SIMULATION) {
            if (IsKeyPressed(KEY_D)) currentState = AppState::MAIN_MENU;
            if (IsKeyPressed(KEY_SPACE)) isPaused = !isPaused;
            if (IsKeyPressed(KEY_I)) showInfoOverlay = !showInfoOverlay;
            
            if (IsKeyPressed(KEY_R)) {
                if (is2DMode) sim2D = Simulator2D(mySettings);
                else sim1D = Simulator1D(mySettings);
                isPaused = true; 
            }
            if (IsKeyPressed(KEY_M)) {
                mySettings.mode = (mySettings.mode == SimMode::QUANTUM) ? SimMode::CLASSICAL : SimMode::QUANTUM;
                if (is2DMode) sim2D.config.mode = mySettings.mode; 
                else sim1D.config.mode = mySettings.mode; 
                isPaused = false; 
            }
            // --- NEW: Metrics Menu Logic ---
            if (is2DMode && IsKeyPressed(KEY_Z)) {
                sim2D.show_metrics_menu = !sim2D.show_metrics_menu;
                isPaused = sim2D.show_metrics_menu; // Auto-pause while menu is open
            }
            if (is2DMode && sim2D.show_metrics_menu) {
                if (IsKeyPressed(KEY_ONE)) sim2D.track_masked_mse = !sim2D.track_masked_mse;
                if (IsKeyPressed(KEY_TWO)) sim2D.track_emd = !sim2D.track_emd;
                if (IsKeyPressed(KEY_THREE)) sim2D.show_historical_overlay = !sim2D.show_historical_overlay;
            }
            if (!isPaused && !showInfoOverlay) {
                for (int s = 0; s < mySettings.steps_per_frame; s++) {
                    if (is2DMode) sim2D.update();
                    else sim1D.update();
                }
            }
        } 
        else if (currentState == AppState::MAIN_MENU) {
            if (IsKeyPressed(KEY_DOWN)) menuSelection = (menuSelection + 1) % 3;
            if (IsKeyPressed(KEY_UP)) menuSelection = (menuSelection - 1 + 3) % 3;
            if (IsKeyPressed(KEY_BACKSPACE)) currentState = AppState::DIMENSION_SELECT; 

            if (IsKeyPressed(KEY_ENTER)) {
                if (menuSelection == 0) {
                    currentState = is2DMode ? AppState::SETUP_2D_SANDBOX : AppState::SETUP_SANDBOX;
                    sandboxSelection = 0; 
                }
                if (menuSelection == 1) {
                    if (is2DMode) {
                        currentState = AppState::VIRUS_DATASET_SELECT;
                        virusMenuSelection = 0;
                    } else {
                        currentState = AppState::SETUP_FIREBREAK;
                    }
                }
                if (menuSelection == 2) currentState = AppState::SETUP_SEARCH;
            }
        }
        else if (currentState == AppState::SETUP_SANDBOX) {
            if (IsKeyPressed(KEY_DOWN)) sandboxSelection = (sandboxSelection + 1) % 4;
            if (IsKeyPressed(KEY_UP)) sandboxSelection = (sandboxSelection - 1 + 4) % 4;

            int slideSpeed = IsKeyDown(KEY_LEFT_SHIFT) ? 50 : 10;

            if (sandboxSelection == 0 && (IsKeyPressed(KEY_LEFT) || IsKeyPressed(KEY_RIGHT))) {
                mySettings.initial_coin = (mySettings.initial_coin == CoinState::RIGHT_HEAVY) ? CoinState::SYMMETRIC : CoinState::RIGHT_HEAVY;
            }
            if (sandboxSelection == 1) {
                if (IsKeyPressed(KEY_RIGHT)) mySettings.resolution += slideSpeed;
                if (IsKeyPressed(KEY_LEFT)) mySettings.resolution -= slideSpeed;
                if (mySettings.resolution < 10) mySettings.resolution = 10; 
            }
            if (sandboxSelection == 2) {
                if (IsKeyPressed(KEY_RIGHT)) mySettings.steps_per_frame++;
                if (IsKeyPressed(KEY_LEFT) && mySettings.steps_per_frame > 1) mySettings.steps_per_frame--;
            }

            if (IsKeyPressed(KEY_ENTER) && sandboxSelection == 3) {
                mySettings.mode = SimMode::QUANTUM;
                mySettings.num_barriers = 0; 
                mySettings.target_position = -1;
                sim1D = Simulator1D(mySettings);
                isPaused = true; 
                currentState = AppState::SIMULATION;
            }
            if (IsKeyPressed(KEY_D)) currentState = AppState::MAIN_MENU;
        }
        else if (currentState == AppState::SETUP_2D_SANDBOX) {
            if (IsKeyPressed(KEY_DOWN)) sandboxSelection = (sandboxSelection + 1) % 7; 
            if (IsKeyPressed(KEY_UP)) sandboxSelection = (sandboxSelection - 1 + 7) % 7;

            int slideSpeed = IsKeyDown(KEY_LEFT_SHIFT) ? 50 : 10;

            if (sandboxSelection == 0 && (IsKeyPressed(KEY_LEFT) || IsKeyPressed(KEY_RIGHT))) {
                mySettings.system_type_2d = (mySettings.system_type_2d == SystemType::CLOSED_UNITARY) 
                                            ? SystemType::OPEN 
                                            : SystemType::CLOSED_UNITARY;
            }
            if (sandboxSelection == 1 && (IsKeyPressed(KEY_LEFT) || IsKeyPressed(KEY_RIGHT))) {
                if (mySettings.system_type_2d == SystemType::CLOSED_UNITARY) {
                    int current = (int)mySettings.unitary_coin;
                    mySettings.unitary_coin = static_cast<UnitaryCoin2D>((current + 1) % 4);
                } 
                else {
                    int current = (int)mySettings.non_unitary_coin;
                    mySettings.non_unitary_coin = static_cast<NonUnitaryCoin2D>((current + 1) % 1); 
                }
            }
            if (sandboxSelection == 2 && (IsKeyPressed(KEY_LEFT) || IsKeyPressed(KEY_RIGHT))) {
                int current = (int)mySettings.init_state_2d;
                mySettings.init_state_2d = static_cast<InitialState2D>((current + 1) % 5); 
            }
            if (sandboxSelection == 3) {
                if (IsKeyPressed(KEY_RIGHT)) mySettings.resolution += slideSpeed;
                if (IsKeyPressed(KEY_LEFT)) mySettings.resolution -= slideSpeed;
                if (mySettings.resolution < 10) mySettings.resolution = 10;
            }
            if (sandboxSelection == 4) {
                if (IsKeyPressed(KEY_RIGHT)) mySettings.steps_per_frame++;
                if (IsKeyPressed(KEY_LEFT) && mySettings.steps_per_frame > 1) mySettings.steps_per_frame--;
            }
            if (sandboxSelection == 5 && (IsKeyPressed(KEY_LEFT) || IsKeyPressed(KEY_RIGHT))) {
                mySettings.boundary_condition = (mySettings.boundary_condition == BoundaryType::ABSORBING) ? BoundaryType::REFLECTIVE : BoundaryType::ABSORBING;
            }
            if (IsKeyPressed(KEY_ENTER) && sandboxSelection == 6) { 
                mySettings.mode = SimMode::QUANTUM;
                mySettings.num_barriers = 0; 
                mySettings.target_position = -1;
                sim2D = Simulator2D(mySettings);
                isPaused = true; 
                currentState = AppState::SIMULATION;
            }
            if (IsKeyPressed(KEY_D)) currentState = AppState::MAIN_MENU;
        }
        else if (currentState == AppState::SETUP_FIREBREAK) {
            DrawRectangle(0, 0, screenWidth, screenHeight, { 30, 40, 50, 255 });
            DrawText("CONFIGURE FIREBREAK SCENARIO", screenWidth/2 - MeasureText("CONFIGURE FIREBREAK SCENARIO", 30)/2, 100, 30, WHITE);
            
            int start_x = screenWidth/2 - 200;
            int draw_y = 200;

            DrawText(TextFormat("Wave Start Node (x):    < %d >", fb_start - 200), start_x, draw_y, 20, (setupSelection == 0) ? YELLOW : GRAY);
            draw_y += 40;
            DrawText(TextFormat("Target Town Node (x):   < %d >", fb_target - 200), start_x, draw_y, 20, (setupSelection == 1) ? YELLOW : GRAY);
            draw_y += 40;
            DrawText(TextFormat("Number of Barriers:     < %d >", fb_num_barriers), start_x, draw_y, 20, (setupSelection == 2) ? YELLOW : GRAY);
            draw_y += 40;

            for (int i = 0; i < fb_num_barriers; i++) {
                DrawText(TextFormat("Barrier %d Node (x):      < %d >", i+1, fb_barriers[i] - 200), start_x, draw_y, 20, (setupSelection == 3 + i) ? YELLOW : GRAY);
                draw_y += 40;
            }

            draw_y += 20; 
            DrawText("[ LAUNCH SCENARIO ]", screenWidth/2 - MeasureText("[ LAUNCH SCENARIO ]", 20)/2, draw_y, 20, (setupSelection == 3 + fb_num_barriers) ? GREEN : GRAY);
            DrawText("Hold SHIFT to slide values faster by 10", screenWidth/2 - MeasureText("Hold SHIFT to slide values faster by 10", 15)/2, 550, 15, LIGHTGRAY);
        }
        else if (currentState == AppState::SETUP_SEARCH) {
            DrawRectangle(0, 0, screenWidth, screenHeight, { 30, 40, 50, 255 });
            DrawText("CONFIGURE SPATIAL SEARCH", screenWidth/2 - MeasureText("CONFIGURE SPATIAL SEARCH", 30)/2, 100, 30, WHITE);
            
            DrawText(TextFormat("Hidden Target Node (x):   < %d >", search_coord - 200), screenWidth/2 - 200, 250, 20, YELLOW);
            
            DrawText("[ LAUNCH ALGORITHM ]", screenWidth/2 - MeasureText("[ LAUNCH ALGORITHM ]", 20)/2, 350, 20, GREEN);
            DrawText("Hold SHIFT to slide values faster by 10", screenWidth/2 - MeasureText("Hold SHIFT to slide values faster by 10", 15)/2, 550, 15, LIGHTGRAY);
        }
        else if (currentState == AppState::VIRUS_DATASET_SELECT) {
            if (IsKeyPressed(KEY_DOWN) || IsKeyPressed(KEY_UP)) virusMenuSelection = (virusMenuSelection + 1) % 2;
            if (IsKeyPressed(KEY_D)) currentState = AppState::MAIN_MENU;

            if (IsKeyPressed(KEY_ENTER)) {
                if (virusMenuSelection == 0) {
                    selectedDataset = "JHU COVID-19 (Global)";
                    currentState = AppState::VIRUS_CONFIG_SETUP;
                    virusConfigSelection = 0;
                } else {
                    currentState = AppState::MAIN_MENU; 
                }
            }
        }
        else if (currentState == AppState::VIRUS_CONFIG_SETUP) {
            // Exactly 13 options (indices 0 to 12)
            if (IsKeyPressed(KEY_DOWN)) virusConfigSelection = (virusConfigSelection + 1) % 13;
            if (IsKeyPressed(KEY_UP)) virusConfigSelection = (virusConfigSelection - 1 + 13) % 13;
            if (IsKeyPressed(KEY_D)) currentState = AppState::MAIN_MENU;

            int slideSpeed = IsKeyDown(KEY_LEFT_SHIFT) ? 50 : 10;

            // 0. Time Step
            if (virusConfigSelection == 0 && (IsKeyPressed(KEY_LEFT) || IsKeyPressed(KEY_RIGHT))) {
                timeStepMode = (timeStepMode + 1) % 3;
            }
            // 1. Bounds Preset
            if (virusConfigSelection == 1 && (IsKeyPressed(KEY_LEFT) || IsKeyPressed(KEY_RIGHT))) {
                mapPresetMode = (mapPresetMode + 1) % 3;
            }
            // 2. Landscape Mode
            if (virusConfigSelection == 2 && (IsKeyPressed(KEY_LEFT) || IsKeyPressed(KEY_RIGHT))) {
                landscapeMode = (landscapeMode + 1) % 1; 
            }
            // 3. Start Day
            if (virusConfigSelection == 3) {
                int dateJump = IsKeyDown(KEY_LEFT_SHIFT) ? 30 : 1; 
                if (IsKeyPressed(KEY_RIGHT)) mySettings.start_day_index += dateJump;
                if (IsKeyPressed(KEY_LEFT)) mySettings.start_day_index -= dateJump;
                if (mySettings.start_day_index < 0) mySettings.start_day_index = 0; 
            }
            // 4. Unitary Coin
            if (virusConfigSelection == 4 && (IsKeyPressed(KEY_LEFT) || IsKeyPressed(KEY_RIGHT))) {
                int current = (int)mySettings.unitary_coin;
                mySettings.unitary_coin = static_cast<UnitaryCoin2D>((current + 1) % 4);
            }
            // 5. Initial Spatial State
            if (virusConfigSelection == 5 && (IsKeyPressed(KEY_LEFT) || IsKeyPressed(KEY_RIGHT))) {
                int current = (int)mySettings.init_state_2d;
                mySettings.init_state_2d = static_cast<InitialState2D>((current + 1) % 5); 
            }
            // 6. Nodal Retention Toggle
            if (virusConfigSelection == 6 && (IsKeyPressed(KEY_LEFT) || IsKeyPressed(KEY_RIGHT))) {
                mySettings.nodal_retention = !mySettings.nodal_retention;
            }
            // 7. Mobility Rate
            if (virusConfigSelection == 7) {
                float slideAmount = IsKeyDown(KEY_LEFT_SHIFT) ? 0.05f : 0.01f;
                if (IsKeyPressed(KEY_RIGHT)) mySettings.mobility_rate += slideAmount;
                if (IsKeyPressed(KEY_LEFT)) mySettings.mobility_rate -= slideAmount;
                if (mySettings.mobility_rate > 1.0f) mySettings.mobility_rate = 1.0f;
                if (mySettings.mobility_rate < 0.0f) mySettings.mobility_rate = 0.0f;
            } 
            // 8. Grid Resolution
            if (virusConfigSelection == 8) {
                if (IsKeyPressed(KEY_RIGHT)) mySettings.resolution += slideSpeed;
                if (IsKeyPressed(KEY_LEFT)) mySettings.resolution -= slideSpeed;
                if (mySettings.resolution < 10) mySettings.resolution = 10;
            }
            // 9. Base Survival Rate
            if (virusConfigSelection == 9) {
                float slideAmount = IsKeyDown(KEY_LEFT_SHIFT) ? 0.05f : 0.01f;
                if (IsKeyPressed(KEY_RIGHT)) mySettings.base_survival_rate += slideAmount;
                if (IsKeyPressed(KEY_LEFT)) mySettings.base_survival_rate -= slideAmount;
            }
            // 10. Urban Multiplier
            if (virusConfigSelection == 10) {
                float slideAmount = IsKeyDown(KEY_LEFT_SHIFT) ? 0.05f : 0.01f;
                if (IsKeyPressed(KEY_RIGHT)) mySettings.urban_multiplier += slideAmount;
                if (IsKeyPressed(KEY_LEFT)) mySettings.urban_multiplier -= slideAmount;
            }
            // 11. Time Scale
            if (virusConfigSelection == 11) {
                if (IsKeyPressed(KEY_RIGHT)) mySettings.quantum_ticks_per_real_tick++;
                if (IsKeyPressed(KEY_LEFT) && mySettings.quantum_ticks_per_real_tick > 1) mySettings.quantum_ticks_per_real_tick--;
            }
            // 12. Boot Simulator
            if (IsKeyPressed(KEY_ENTER) && virusConfigSelection == 12) {
                mySettings.mode = SimMode::QUANTUM;
                mySettings.system_type_2d = SystemType::OPEN;
                
                if (mapPresetMode == 0) {
                    mySettings.min_lat = -90.0f; mySettings.max_lat = 90.0f;
                    mySettings.min_lon = -180.0f; mySettings.max_lon = 180.0f;
                } else if (mapPresetMode == 1) {
                    mySettings.min_lat = 24.0f; mySettings.max_lat = 50.0f;
                    mySettings.min_lon = -125.0f; mySettings.max_lon = -66.0f;
                } else if (mapPresetMode == 2) {
                    mySettings.min_lat = 35.0f; mySettings.max_lat = 60.0f;
                    mySettings.min_lon = -10.0f; mySettings.max_lon = 30.0f;
                }
                
                std::string target_file = "data/time_series_covid19_confirmed_us.csv";
                std::vector<ViralHotspot> raw_data = ParseDiseaseData(target_file, timeStepMode, mySettings.start_day_index);
                
                sim2D = Simulator2D(mySettings);
                sim2D.inject_dataset(raw_data); 
                
                isPaused = true; 
                currentState = AppState::SIMULATION;
            }
        }

        // ==========================================
        // DRAWING PHASE
        // ==========================================
        BeginDrawing();
        ClearBackground({ 245, 245, 245, 255 });

        if (currentState == AppState::DIMENSION_SELECT) {
            DrawRectangle(0, 0, screenWidth, screenHeight, { 20, 20, 20, 255 }); 
            DrawText("QUANTUM WALK VIRTUAL MACHINE", screenWidth/2 - MeasureText("QUANTUM WALK VIRTUAL MACHINE", 30)/2, 150, 30, WHITE);
            DrawText("SELECT COMPUTE DIMENSION:", screenWidth/2 - MeasureText("SELECT COMPUTE DIMENSION:", 20)/2, 250, 20, LIGHTGRAY);
            DrawText("> 1D TENSOR ENGINE", screenWidth/2 - 120, 320, 20, (dimSelection == 0) ? YELLOW : GRAY);
            DrawText("> 2D GROVER ENGINE", screenWidth/2 - 120, 360, 20, (dimSelection == 1) ? YELLOW : GRAY);
        }
        else if (currentState == AppState::SIMULATION) {
            if (is2DMode) {
                Vector2 mousePos = GetMousePosition();
                bool isMouseDown = IsMouseButtonDown(MOUSE_BUTTON_LEFT);
                sim2D.draw(screenWidth, screenHeight, showInfoOverlay, isPaused, mousePos, isMouseDown);
            }
            else sim1D.draw(screenWidth, screenHeight, showInfoOverlay);

            const char* modeText = (mySettings.mode == SimMode::QUANTUM) ? "MODE: QUANTUM WALK" : "MODE: CLASSICAL";
            
            int boxWidth = MeasureText(modeText, 20) + 20; 
            DrawRectangle(10, 10, boxWidth, 40, {30, 30, 30, 255});
            DrawText(modeText, 20, 20, 20, {245, 245, 245, 255});

            DrawText("[M] Toggle | [SPACE] Pause | [R] Reset | [D] Demos | [I] Diagnostics", 10, 60, 10, {100, 100, 100, 255});
            if (isPaused) DrawText("PAUSED", screenWidth / 2 - MeasureText("PAUSED", 20) / 2, 20, 20, { 180, 50, 50, 255 });
        }
        else if (currentState == AppState::MAIN_MENU) {
            DrawRectangle(0, 0, screenWidth, screenHeight, { 20, 20, 20, 240 }); 
            const char* menuTitle = is2DMode ? "2D ENGINE DEMOS" : "1D ENGINE DEMOS";
            DrawText(menuTitle, screenWidth/2 - MeasureText(menuTitle, 40)/2, 150, 40, WHITE);
            DrawText("> Sandbox Environment", screenWidth/2 - 150, 250, 20, (menuSelection == 0) ? YELLOW : GRAY);
            if (is2DMode) DrawText("> Demo: Disease Spread (Viruses)", screenWidth/2 - 150, 300, 20, (menuSelection == 1) ? YELLOW : GRAY);
            else DrawText("> Demo: Algorithmic Firebreaks", screenWidth/2 - 150, 300, 20, (menuSelection == 1) ? YELLOW : GRAY);
            DrawText("> Demo: Spatial Search", screenWidth/2 - 150, 350, 20, (menuSelection == 2) ? YELLOW : GRAY);
            DrawText("Use UP/DOWN to navigate, ENTER to select, BACKSPACE to change dimension", screenWidth/2 - 380, 500, 20, DARKGRAY);
        }
        else if (currentState == AppState::SETUP_SANDBOX) {
            DrawRectangle(0, 0, screenWidth, screenHeight, { 30, 40, 50, 255 });
            DrawText("CONFIGURE 1D SANDBOX", screenWidth/2 - MeasureText("CONFIGURE 1D SANDBOX", 30)/2, 80, 30, WHITE);
            
            int col1 = screenWidth/2 - 250;
            int col2 = screenWidth/2 - 10;

            DrawText("INITIAL COIN STATE:", col1, 200, 20, (sandboxSelection == 0) ? YELLOW : GRAY);
            const char* coinText = (mySettings.initial_coin == CoinState::RIGHT_HEAVY) ? "< RIGHT HEAVY (Broken) >" : "< SYMMETRIC (Balanced) >";
            DrawText(coinText, col2, 200, 20, (sandboxSelection == 0) ? WHITE : LIGHTGRAY);

            DrawText("GRID RESOLUTION:", col1, 250, 20, (sandboxSelection == 1) ? YELLOW : GRAY);
            DrawText(TextFormat("< %d Nodes >", mySettings.resolution), col2, 250, 20, (sandboxSelection == 1) ? WHITE : LIGHTGRAY);

            DrawText("SIMULATION SPEED:", col1, 300, 20, (sandboxSelection == 2) ? YELLOW : GRAY);
            DrawText(TextFormat("< %d Steps / Frame >", mySettings.steps_per_frame), col2, 300, 20, (sandboxSelection == 2) ? WHITE : LIGHTGRAY);

            DrawText("[ LAUNCH SANDBOX ]", screenWidth/2 - MeasureText("[ LAUNCH SANDBOX ]", 20)/2, 450, 20, (sandboxSelection == 3) ? GREEN : GRAY);
            DrawText("Hold SHIFT to adjust resolution faster", screenWidth/2 - MeasureText("Hold SHIFT to adjust resolution faster", 15)/2, 550, 15, LIGHTGRAY);
        }
        else if (currentState == AppState::SETUP_2D_SANDBOX) {
            DrawRectangle(0, 0, screenWidth, screenHeight, { 30, 40, 50, 255 });
            DrawText("CONFIGURE 2D SANDBOX", screenWidth/2 - MeasureText("CONFIGURE 2D SANDBOX", 30)/2, 80, 30, WHITE);
            
            int col1 = screenWidth/2 - 250;
            int col2 = screenWidth/2 + 10;

            DrawText("SYSTEM TYPE:", col1, 150, 20, (sandboxSelection == 0) ? YELLOW : GRAY);
            const char* sysText = (mySettings.system_type_2d == SystemType::CLOSED_UNITARY) ? "< CLOSED (UNITARY COINS) >" : "< OPEN (NON-UNITARY COINS) >";
            DrawText(sysText, col2, 150, 20, (sandboxSelection == 0) ? WHITE : LIGHTGRAY);

            DrawText("COIN OPERATOR:", col1, 190, 20, (sandboxSelection == 1) ? YELLOW : GRAY);
            const char* coinText = "";
            if (mySettings.system_type_2d == SystemType::CLOSED_UNITARY) {
                if (mySettings.unitary_coin == UnitaryCoin2D::GROVER) coinText = "< GROVER >";
                else if (mySettings.unitary_coin == UnitaryCoin2D::DFT) coinText = "< DISCRETE FOURIER TRANSFORM >";
                else if (mySettings.unitary_coin == UnitaryCoin2D::HADAMARD_TENSOR) coinText = "< HADAMARD TENSOR PRODUCT >";
                else coinText = "< ALTERNATING DFT >";
            } 
            else {
                if (mySettings.non_unitary_coin == NonUnitaryCoin2D::EPIDEMIC_SCALAR) coinText = "< EPIDEMIC SCALAR (R0) >";
            }
            DrawText(coinText, col2, 190, 20, (sandboxSelection == 1) ? WHITE : LIGHTGRAY);

            DrawText("INITIAL SPATIAL STATE:", col1, 230, 20, (sandboxSelection == 2) ? YELLOW : GRAY);
            const char* stateText = "";
            if (mySettings.init_state_2d == InitialState2D::PURE_NORTH) stateText = "< PURE NORTH >";
            else if (mySettings.init_state_2d == InitialState2D::UNIFORM) stateText = "< UNIFORM >";
            else if (mySettings.init_state_2d == InitialState2D::ALTERNATING_PHASE) stateText = "< ALTERNATING PHASE >";
            else if (mySettings.init_state_2d == InitialState2D::HADAMARD_SYMMETRIC) stateText = "< HADAMARD SYMMETRIC >";
            else stateText = "< CHIRAL WEST >";
            DrawText(stateText, col2, 230, 20, (sandboxSelection == 2) ? WHITE : LIGHTGRAY);

            DrawText("GRID RESOLUTION:", col1, 270, 20, (sandboxSelection == 3) ? YELLOW : GRAY);
            DrawText(TextFormat("< %dx%d Nodes >", mySettings.resolution, mySettings.resolution), col2, 270, 20, (sandboxSelection == 3) ? WHITE : LIGHTGRAY);

            DrawText("SIMULATION SPEED:", col1, 310, 20, (sandboxSelection == 4) ? YELLOW : GRAY);
            DrawText(TextFormat("< %d Steps / Frame >", mySettings.steps_per_frame), col2, 310, 20, (sandboxSelection == 4) ? WHITE : LIGHTGRAY);

            DrawText("BOUNDARY CONDITION:", col1, 350, 20, (sandboxSelection == 5) ? YELLOW : GRAY);
            const char* boundsText = (mySettings.boundary_condition == BoundaryType::ABSORBING) ? "< ABSORBING (Void) >" : "< REFLECTIVE (Walls) >";
            DrawText(boundsText, col2, 350, 20, (sandboxSelection == 5) ? WHITE : LIGHTGRAY);

            DrawText("[ LAUNCH SANDBOX ]", screenWidth/2 - MeasureText("[ LAUNCH SANDBOX ]", 20)/2, 450, 20, (sandboxSelection == 6) ? GREEN : GRAY);
            DrawText("Hold SHIFT to adjust resolution faster", screenWidth/2 - MeasureText("Hold SHIFT to adjust resolution faster", 15)/2, 520, 15, LIGHTGRAY);
        }
        else if (currentState == AppState::SETUP_FIREBREAK) {
            DrawRectangle(0, 0, screenWidth, screenHeight, { 30, 40, 50, 255 });
            DrawText("CONFIGURE FIREBREAK SCENARIO", screenWidth/2 - MeasureText("CONFIGURE FIREBREAK SCENARIO", 30)/2, 100, 30, WHITE);
            
            int start_x = screenWidth/2 - 200;
            int draw_y = 200;

            DrawText(TextFormat("Wave Start Node (x):    < %d >", fb_start - 200), start_x, draw_y, 20, (setupSelection == 0) ? YELLOW : GRAY);
            draw_y += 40;
            DrawText(TextFormat("Target Town Node (x):   < %d >", fb_target - 200), start_x, draw_y, 20, (setupSelection == 1) ? YELLOW : GRAY);
            draw_y += 40;
            DrawText(TextFormat("Number of Barriers:     < %d >", fb_num_barriers), start_x, draw_y, 20, (setupSelection == 2) ? YELLOW : GRAY);
            draw_y += 40;

            for (int i = 0; i < fb_num_barriers; i++) {
                DrawText(TextFormat("Barrier %d Node (x):      < %d >", i+1, fb_barriers[i] - 200), start_x, draw_y, 20, (setupSelection == 3 + i) ? YELLOW : GRAY);
                draw_y += 40;
            }

            draw_y += 20; 
            DrawText("[ LAUNCH SCENARIO ]", screenWidth/2 - MeasureText("[ LAUNCH SCENARIO ]", 20)/2, draw_y, 20, (setupSelection == 3 + fb_num_barriers) ? GREEN : GRAY);
            DrawText("Hold SHIFT to slide values faster by 10", screenWidth/2 - MeasureText("Hold SHIFT to slide values faster by 10", 15)/2, 550, 15, LIGHTGRAY);
        }
        else if (currentState == AppState::SETUP_SEARCH) {
            DrawRectangle(0, 0, screenWidth, screenHeight, { 30, 40, 50, 255 });
            DrawText("CONFIGURE SPATIAL SEARCH", screenWidth/2 - MeasureText("CONFIGURE SPATIAL SEARCH", 30)/2, 100, 30, WHITE);
            
            DrawText(TextFormat("Hidden Target Node (x):   < %d >", search_coord - 200), screenWidth/2 - 200, 250, 20, YELLOW);
            
            DrawText("[ LAUNCH ALGORITHM ]", screenWidth/2 - MeasureText("[ LAUNCH ALGORITHM ]", 20)/2, 350, 20, GREEN);
            DrawText("Hold SHIFT to slide values faster by 10", screenWidth/2 - MeasureText("Hold SHIFT to slide values faster by 10", 15)/2, 550, 15, LIGHTGRAY);
        }
        else if (currentState == AppState::VIRUS_DATASET_SELECT) {
            if (IsKeyPressed(KEY_DOWN) || IsKeyPressed(KEY_UP)) virusMenuSelection = (virusMenuSelection + 1) % 2;
            if (IsKeyPressed(KEY_D)) currentState = AppState::MAIN_MENU;

            if (IsKeyPressed(KEY_ENTER)) {
                if (virusMenuSelection == 0) {
                    selectedDataset = "JHU COVID-19 (Global)";
                    currentState = AppState::VIRUS_CONFIG_SETUP;
                    virusConfigSelection = 0;
                } else {
                    currentState = AppState::MAIN_MENU; 
                }
            }
        }
        else if (currentState == AppState::VIRUS_CONFIG_SETUP) {
            DrawRectangle(0, 0, screenWidth, screenHeight, { 30, 40, 50, 255 });
            DrawText("CONFIGURE DATA INGESTION", screenWidth/2 - MeasureText("CONFIGURE DATA INGESTION", 30)/2, 40, 30, WHITE);
            DrawText(TextFormat("Dataset: %s", selectedDataset), screenWidth/2 - MeasureText(TextFormat("Dataset: %s", selectedDataset), 20)/2, 80, 20, LIGHTGRAY);
            
            int col1 = screenWidth/2 - 260; 
            int col2 = screenWidth/2 + 30;

            int start_y = 110;
            int y_step = 35;

            DrawText("TIME STEP AGGREGATION:", col1, start_y, 20, (virusConfigSelection == 0) ? YELLOW : GRAY);
            const char* timeText = (timeStepMode == 0) ? "< DAILY >" : (timeStepMode == 1) ? "< WEEKLY >" : "< MONTHLY >";
            DrawText(timeText, col2, start_y, 20, (virusConfigSelection == 0) ? WHITE : LIGHTGRAY);

            DrawText("GEOGRAPHIC BOUNDS:", col1, start_y + y_step*1, 20, (virusConfigSelection == 1) ? YELLOW : GRAY);
            const char* boundsText = (mapPresetMode == 0) ? "< GLOBAL >" : (mapPresetMode == 1) ? "< UNITED STATES >" : "< EUROPE >";
            DrawText(boundsText, col2, start_y + y_step*1, 20, (virusConfigSelection == 1) ? WHITE : LIGHTGRAY);

            DrawText("ENERGY LANDSCAPE:", col1, start_y + y_step*2, 20, (virusConfigSelection == 2) ? YELLOW : GRAY);
            DrawText("< NASA SEDAC POPULATION GRID >", col2, start_y + y_step*2, 20, (virusConfigSelection == 2) ? WHITE : LIGHTGRAY);

            DrawText("OUTBREAK START DATE:", col1, start_y + y_step*3, 20, (virusConfigSelection == 3) ? YELLOW : GRAY);
            DrawText(TextFormat("< Day %d >", mySettings.start_day_index), col2, start_y + y_step*3, 20, (virusConfigSelection == 3) ? WHITE : LIGHTGRAY);

            DrawText("QUANTUM COIN:", col1, start_y + y_step*4, 20, (virusConfigSelection == 4) ? YELLOW : GRAY);
            const char* coinText = "";
            if (mySettings.unitary_coin == UnitaryCoin2D::GROVER) coinText = "< GROVER >";
            else if (mySettings.unitary_coin == UnitaryCoin2D::DFT) coinText = "< DISCRETE FOURIER TRANSFORM >";
            else if (mySettings.unitary_coin == UnitaryCoin2D::HADAMARD_TENSOR) coinText = "< HADAMARD TENSOR >";
            else coinText = "< ALTERNATING DFT >";
            DrawText(coinText, col2, start_y + y_step*4, 20, (virusConfigSelection == 4) ? WHITE : LIGHTGRAY);

            DrawText("INITIAL SPATIAL STATE:", col1, start_y + y_step*5, 20, (virusConfigSelection == 5) ? YELLOW : GRAY);
            const char* stateText = "";
            if (mySettings.init_state_2d == InitialState2D::PURE_NORTH) stateText = "< PURE NORTH >";
            else if (mySettings.init_state_2d == InitialState2D::UNIFORM) stateText = "< UNIFORM >";
            else if (mySettings.init_state_2d == InitialState2D::ALTERNATING_PHASE) stateText = "< ALTERNATING PHASE >";
            else if (mySettings.init_state_2d == InitialState2D::HADAMARD_SYMMETRIC) stateText = "< HADAMARD SYMMETRIC >";
            else stateText = "< CHIRAL WEST >";
            DrawText(stateText, col2, start_y + y_step*5, 20, (virusConfigSelection == 5) ? WHITE : LIGHTGRAY);

            DrawText("NODAL RETENTION (Pooling):", col1, start_y + y_step*6, 20, (virusConfigSelection == 6) ? YELLOW : GRAY);
            DrawText(mySettings.nodal_retention ? "< ENABLED >" : "< DISABLED >", col2, start_y + y_step*6, 20, (virusConfigSelection == 6) ? WHITE : LIGHTGRAY);

            DrawText("MOBILITY RATE:", col1, start_y + y_step*7, 20, (virusConfigSelection == 7) ? YELLOW : GRAY);
            DrawText(TextFormat("< %.0f%% Moves / Tick >", mySettings.mobility_rate * 100.0f), col2, start_y + y_step*7, 20, (virusConfigSelection == 7) ? WHITE : LIGHTGRAY);

            DrawText("GRID RESOLUTION:", col1, start_y + y_step*8, 20, (virusConfigSelection == 8) ? YELLOW : GRAY);
            DrawText(TextFormat("< %dx%d Nodes >", mySettings.resolution, mySettings.resolution), col2, start_y + y_step*8, 20, (virusConfigSelection == 8) ? WHITE : LIGHTGRAY);

            DrawText("BASE SURVIVAL RATE (Decay):", col1, start_y + y_step*9, 20, (virusConfigSelection == 9) ? YELLOW : GRAY);
            DrawText(TextFormat("< %.2f >", mySettings.base_survival_rate), col2, start_y + y_step*9, 20, (virusConfigSelection == 9) ? WHITE : LIGHTGRAY);

            DrawText("URBAN R0 MULTIPLIER:", col1, start_y + y_step*10, 20, (virusConfigSelection == 10) ? YELLOW : GRAY);
            DrawText(TextFormat("< +%.2f >", mySettings.urban_multiplier), col2, start_y + y_step*10, 20, (virusConfigSelection == 10) ? WHITE : LIGHTGRAY);

            DrawText("TIME SCALE RATIO:", col1, start_y + y_step*11, 20, (virusConfigSelection == 11) ? YELLOW : GRAY);
            DrawText(TextFormat("< %d Quantum Ticks / Day >", mySettings.quantum_ticks_per_real_tick), col2, start_y + y_step*11, 20, (virusConfigSelection == 11) ? WHITE : LIGHTGRAY);

            DrawText("[ BOOT SIMULATOR ]", screenWidth/2 - MeasureText("[ BOOT SIMULATOR ]", 20)/2, start_y + y_step*13, 20, (virusConfigSelection == 12) ? GREEN : GRAY);
            DrawText("Hold SHIFT to adjust numbers faster", screenWidth/2 - MeasureText("Hold SHIFT to adjust numbers faster", 15)/2, start_y + y_step*14, 15, LIGHTGRAY);
        }
        else {
            DrawRectangle(0, 0, screenWidth, screenHeight, { 30, 40, 50, 255 });
            DrawText("UI Config Not Fully Handled Here. Press D to return.", 20, 20, 20, RED);
            if (IsKeyPressed(KEY_D)) currentState = AppState::MAIN_MENU;
        }
        
        EndDrawing();
    }

    CloseWindow();
    return 0;
}
