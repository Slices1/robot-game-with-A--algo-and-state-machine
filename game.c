#include "raylib.h"
#include "rlgl.h"
#include "raymath.h"
#include <stdlib.h>
#include <stdio.h> // For sprintf, file handling
#include <string.h> // For strings
#include <ctype.h> // For isalnum()

//--------------------------------------------------------------------------------------
// Constants & Definitions
//--------------------------------------------------------------------------------------
    #define GRID_WIDTH 30
    #define GRID_HEIGHT 30
    #define CELL_SIZE 2.0f
    #define MAX_LIVES 5
    #define NUM_PEOPLE 5
    #define BATTERY_RADIUS (GRID_WIDTH * CELL_SIZE * 0.8f)    
    #define MAX_LEADERBOARD_ENTRIES 100
    #define LEADERBOARD_DISPLAY_LIMIT 5
    #define MAX_PATH_LENGTH (GRID_WIDTH * GRID_HEIGHT) // for weighted A* algo path
    typedef struct {
        char name[20];
        int level;
        int duration; // seconds
    } ScoreEntry;
    
    typedef enum {
        CELL_AIR = 0,
        CELL_WALL,
        CELL_ROBOT,
        CELL_MINE,
        CELL_PERSON,
    } CellType;

    static const Color cellFillColours[] = {DARKGRAY,
                       BLUE,
                       RED,
                       GREEN,
                       };
    static const Color cellOutlineColours[] = {GRAY,
                       DARKGRAY,
                       DARKGRAY,
                       DARKGRAY,
                       };


    typedef enum {
        STATE_MENU,
        STATE_PLAYING,
        STATE_GAME_OVER
    } GameState;

    typedef enum {
        NORTH,
        EAST,
        SOUTH,
        WEST,
    } Direction;

    // The order MUST match the enum order above!
    static const Vector2 DIR_VECTORS[] = {
        { 0, -1 }, // Matches NORTH (0)
        { 1,  0 }, // Matches EAST (1)
        { 0,  1 }, // Matches SOUTH (2)
        {-1,  0 }  // Matches WEST (3)
    };

    typedef struct {
        Vector2 position;
        Direction direction;
        float liklihoodToMove;
        float liklihoodToTurn;
    } MovingEntity;

    typedef struct {
        Vector2 position;
        Direction direction;
        int moveCooldown; // number of frames between robot moves
    } Robot;

    typedef struct {
        Vector3 position;
        bool isActive; // True if this life hasn't been lost yet
        float angle;   // Useful to rotate the battery itself
    } BatteryCell;

    // The Context struct holds all game data so we can pass it around easily
    typedef struct {
        GameState currentState;
        Camera3D camera;
        // The grid stores integers representing each tile.
        // I store the posiitions of the entities in their struct.
        // I will have to keep the grid in sync with their positions in the structs though.
        int grid[GRID_WIDTH][GRID_HEIGHT];
        int currentLevel;
        int score;
        bool paused;
        bool orbitMode;
        Robot robot;
        MovingEntity people[NUM_PEOPLE];
        int mineCount;
        MovingEntity* mines;
        float peopleMaxMovesPerSec;
        float minesMaxMovesPerSec;
        bool aiModeEnabled; // true -> ai mode. false -> manual mode
        int frameCount;
        int peopleRemaining;
        int livesRemaining;

        char username[20]; // Buffer for the name
        int usernameLen;   // Current length of name

        // A*
        Vector2 currentPath[MAX_PATH_LENGTH];
        int currentPathLen;
        float AStarHeuristicWeightage;
        
        // Cursors for gameplay
        Vector2 lastGridCellFocused;
        Vector2 gridCellFocused;
    } GameContext;

//--------------------------------------------------------------------------------------
// Function Forward Declarations
//--------------------------------------------------------------------------------------
    void InitGame(GameContext *ctx);
    void AdvanceLevel(GameContext *ctx); // Clears grid for new level

    // The three "Screen" functions
    void UpdateDrawMenu(GameContext *ctx);
    void UpdateDrawGameplay(GameContext *ctx);
    void UpdateDrawGameOver(GameContext *ctx);

    // helpers
    void DrawSingleBattery(Vector3 pos, float rotationY, bool isActive);
    void DrawBatteries(GameContext *ctx);
    void PaintGridLine(GameContext *ctx, int x0, int y0, int x1, int y1, int value);
    void UpdateCustomCamera(Camera3D *camera, bool *orbitMode);
    void HandleGridInteraction(GameContext *ctx);
    void DrawGameScene(GameContext *ctx);
    int CompareScores(const void *a, const void *b);

    int min(int a, int b);
    int max(int a, int b);

//--------------------------------------------------------------------------------------
// Main Entry Point
//--------------------------------------------------------------------------------------
int main(void) {
    SetConfigFlags(FLAG_WINDOW_RESIZABLE | FLAG_MSAA_4X_HINT);
    InitWindow(800, 450, "Robot Save the People - State Machine, A* Algo");

    // Initialise the Game Context (Camera, vars, etc)
    GameContext ctx = { 0 };
    InitGame(&ctx);

    SetTargetFPS(60);

    while (!WindowShouldClose())
    {
        // THE STATE MACHINE
        switch (ctx.currentState)
        {
            case STATE_MENU:
                UpdateDrawMenu(&ctx);
                break;
            case STATE_PLAYING:
                UpdateDrawGameplay(&ctx);
                break;
            case STATE_GAME_OVER:
                UpdateDrawGameOver(&ctx);
                break;
        }
    }

    CloseWindow();
    return 0;
}

void MoveEntity(GameContext *ctx, MovingEntity *entity, CellType entityCellType, Vector2 *pos, Direction *dir) {
    Vector2* dirVec = &DIR_VECTORS[*dir];
    Vector2 futurePos = Vector2Add(*pos, *dirVec); 
    // check its not outside the grid
    if (   futurePos.x >= GRID_WIDTH  || futurePos.x < 0
        || futurePos.y >= GRID_HEIGHT || futurePos.y < 0) return;
    

    CellType futureCell = ctx->grid[(int)futurePos.x][(int)futurePos.y];
    // if robot collides with person
    if (futureCell == CELL_PERSON) {
        // only robots can interact
        if (entityCellType != CELL_ROBOT) return;

        ctx->peopleRemaining += -1;
        // disable the person
        // do so by first finding them, then setting their coords to invalid values
        for (int i=0; i<NUM_PEOPLE; i++) {
            if (ctx->people[i].position.x == futurePos.x && ctx->people[i].position.y == futurePos.y) {
                ctx->people[i].position = (Vector2){-1, -1};
                break;
            }
        }
        // dont return, which causes the robot to move onwards
    }

    if (((futureCell == CELL_WALL || futureCell == CELL_MINE) && entityCellType == CELL_ROBOT)
         || (futureCell == CELL_ROBOT && entityCellType == CELL_MINE) ) {
            ctx->livesRemaining += -1;
            // reset pos
            ctx->grid[(int)pos->x][(int)pos->y] = CELL_AIR;
            ctx->robot.position = (Vector2){3*GRID_HEIGHT/4, GRID_WIDTH/4};
        if (entityCellType == CELL_ROBOT) return;
    }

    if (futureCell == CELL_WALL) return;

    ctx->grid[(int)pos->x][(int)pos->y] = CELL_AIR;
    *pos = futurePos;
    ctx->grid[(int)futurePos.x][(int)futurePos.y] = entityCellType;
}

void MoveMovingEntity(GameContext *ctx, MovingEntity *entity, CellType entityCellType) {
    // if the position is invalid, the entity is disabled and shouldn't be moved
    if (entity->position.x == -1) return;
    if (rand() < entity->liklihoodToTurn * RAND_MAX) {
        entity->direction = (entity->direction + rand() % 2) % 4;
    }
    if (rand() < entity->liklihoodToMove * RAND_MAX) {
        MoveEntity(ctx, entity, entityCellType, &entity->position, &entity->direction);
    }
}

typedef struct {
    int x, y;
    int gCost; // Distance from start
    int hCost; // Distance to end (Heuristic)
    int fCost; // G + H
    int parentX, parentY; // For retracing the path
    bool closed; // If true, node has been evaluated
    bool open;   // If true, node is in the queue to be evaluated
} Node;

// Simple Manhattan Distance Heuristic
int GetDistance(int x1, int y1, int x2, int y2) {
    return abs(x1 - x2) + abs(y1 - y2);
}

bool IsNearMine(GameContext *ctx, int x, int y) {
    // Check all 8 surrounding neighbors (diagonals included)
    for (int dx = -1; dx <= 1; dx++) {
        for (int dy = -1; dy <= 1; dy++) {
            if (dx == 0 && dy == 0) continue; // Skip the center tile itself
            
            int nx = x + dx;
            int ny = y + dy;
            
            // Bounds check
            if (nx >= 0 && nx < GRID_WIDTH && ny >= 0 && ny < GRID_HEIGHT) {
                if (ctx->grid[nx][ny] == CELL_MINE) return true;
            }
        }
    }
    return false;
}

// Scans a radius around (x,y) to find the distance to the closest mine.
// Returns a high number if safe, low number if dangerous.
int GetLocalSafetyScore(GameContext *ctx, int x, int y, int radius) {
    int closestMineDist = 999;
    
    for (int dx = -radius; dx <= radius; dx++) {
        for (int dy = -radius; dy <= radius; dy++) {
            int nx = x + dx;
            int ny = y + dy;
            
            if (nx >= 0 && nx < GRID_WIDTH && ny >= 0 && ny < GRID_HEIGHT) {
                if (ctx->grid[nx][ny] == CELL_MINE) {
                    int dist = abs(dx) + abs(dy); // Manhattan distance to the hazard
                    if (dist < closestMineDist) closestMineDist = dist;
                }
            }
        }
    }
    return closestMineDist;
}

void move_robot_ai(GameContext *ctx) {
    // 1. CLEAR PREVIOUS PATH
    ctx->currentPathLen = 0;

    // 2. FIND TARGET
    Vector2 startPos = ctx->robot.position;
    Vector2 targetPos = {-1, -1};
    int shortestDist = 99999;

    for (int i = 0; i < 5; i++) {
        if (ctx->people[i].position.x != -1) {
            int dist = GetDistance((int)startPos.x, (int)startPos.y, 
                                   (int)ctx->people[i].position.x, (int)ctx->people[i].position.y);
            if (dist < shortestDist) {
                shortestDist = dist;
                targetPos = ctx->people[i].position;
            }
        }
    }

    // If no target, we skip A* and go straight to fallback
    if (targetPos.x != -1) {

        // 3. INITIALIZE A* DATA
        static Node nodes[GRID_WIDTH][GRID_HEIGHT]; 
        for (int x = 0; x < GRID_WIDTH; x++) {
            for (int y = 0; y < GRID_HEIGHT; y++) {
                nodes[x][y] = (Node){x, y, 9999, 9999, 9999, -1, -1, false, false};
            }
        }

        int startX = (int)startPos.x;
        int startY = (int)startPos.y;
        int targetX = (int)targetPos.x;
        int targetY = (int)targetPos.y;

        nodes[startX][startY].gCost = 0;
        nodes[startX][startY].hCost = GetDistance(startX, startY, targetX, targetY);
        nodes[startX][startY].fCost = nodes[startX][startY].hCost;
        nodes[startX][startY].open = true;

        // 4. MAIN A* LOOP
        while (true) {
            Node* current = NULL;
            int lowestF = 999999;

            for (int x = 0; x < GRID_WIDTH; x++) {
                for (int y = 0; y < GRID_HEIGHT; y++) {
                    if (nodes[x][y].open && nodes[x][y].fCost < lowestF) {
                        current = &nodes[x][y];
                        lowestF = nodes[x][y].fCost;
                    }
                }
            }

            // FIX 1: If no path found, BREAK (don't return) so we can run the fallback logic
            if (current == NULL) break; 

            if (current->x == targetX && current->y == targetY) {
                // Retrace path
                int traceX = targetX;
                int traceY = targetY;
                while (traceX != -1 && traceY != -1) {
                    if (traceX == startX && traceY == startY) break;
                    ctx->currentPath[ctx->currentPathLen] = (Vector2){(float)traceX, (float)traceY};
                    ctx->currentPathLen++;
                    int pX = nodes[traceX][traceY].parentX;
                    int pY = nodes[traceX][traceY].parentY;
                    traceX = pX;
                    traceY = pY;
                }
                break;
            }

            current->open = false;
            current->closed = true;

            int dirX[] = {0, 1, 0, -1};
            int dirY[] = {-1, 0, 1, 0};

            for (int i = 0; i < 4; i++) {
                int checkX = current->x + dirX[i];
                int checkY = current->y + dirY[i];

                if (checkX < 0 || checkX >= GRID_WIDTH || checkY < 0 || checkY >= GRID_HEIGHT) continue;
                
                int cell = ctx->grid[checkX][checkY];
                if (cell == CELL_WALL || cell == CELL_MINE) continue;
                if (nodes[checkX][checkY].closed) continue;

                // FIX 2: Soft Penalty instead of Hard Wall
                // If tile is near a mine, add 20 to the cost (robot will detour if possible)
                // But it WILL go there if it's the only path.
                int dangerPenalty = 0;
                if (IsNearMine(ctx, checkX, checkY)) dangerPenalty = 20;

                int moveCost = nodes[current->x][current->y].gCost + 1 + dangerPenalty;

                if (moveCost < nodes[checkX][checkY].gCost || !nodes[checkX][checkY].open) {
                    nodes[checkX][checkY].gCost = moveCost;
                    // Ensure you have this multiplier variable in your struct, or use 1.5f directly
                    nodes[checkX][checkY].hCost = (int)(GetDistance(checkX, checkY, targetX, targetY) * ctx->AStarHeuristicWeightage); 
                    nodes[checkX][checkY].fCost = nodes[checkX][checkY].gCost + nodes[checkX][checkY].hCost;
                    nodes[checkX][checkY].parentX = current->x;
                    nodes[checkX][checkY].parentY = current->y;
                    nodes[checkX][checkY].open = true;
                }
            }
        }
    }

    // 5. EXECUTE MOVE (Or Fallback)
    if (ctx->currentPathLen > 0) {
        Vector2 nextStep = ctx->currentPath[ctx->currentPathLen - 1];
        
        int dx = (int)nextStep.x - (int)startPos.x;
        int dy = (int)nextStep.y - (int)startPos.y;

        if (dy == -1) ctx->robot.direction = NORTH;
        if (dx == 1)  ctx->robot.direction = EAST;
        if (dy == 1)  ctx->robot.direction = SOUTH;
        if (dx == -1) ctx->robot.direction = WEST;
    }
    else {
        // FALLBACK: Run the Survival Logic
        // (This code remains exactly as we wrote it in the previous step)
        int bestScore = -1;
        Vector2 bestMove = {-1, -1};
        Direction bestDir = ctx->robot.direction; 
        
        int dx[] = {0, 1, 0, -1};
        int dy[] = {-1, 0, 1, 0};
        Direction dirs[] = {NORTH, EAST, SOUTH, WEST};
        int startIdx = rand() % 4;

        for (int i = 0; i < 4; i++) {
            int idx = (startIdx + i) % 4;
            int nx = (int)startPos.x + dx[idx];
            int ny = (int)startPos.y + dy[idx];

            if (nx < 0 || nx >= GRID_WIDTH || ny < 0 || ny >= GRID_HEIGHT) continue;
            if (ctx->grid[nx][ny] == CELL_WALL || ctx->grid[nx][ny] == CELL_MINE) continue;

            int score = GetLocalSafetyScore(ctx, nx, ny, 4);
            if (score > bestScore) {
                bestScore = score;
                bestMove = (Vector2){(float)nx, (float)ny};
                bestDir = dirs[idx];
            }
        }
        
        if (bestMove.x != -1) {
            ctx->robot.direction = bestDir;
            ctx->currentPath[0] = bestMove;
            ctx->currentPathLen = 1;
        } else {
            ctx->robot.direction = (Direction)((ctx->robot.direction + 2) % 4);
        }
    }
}
                
Direction GetCameraForwardDirection(Camera3D camera) {
    Vector3 forward = Vector3Subtract(camera.target, camera.position);
    
    // Determine major axis
    if (fabsf(forward.z) > fabsf(forward.x)) {
        return (forward.z < 0) ? NORTH : SOUTH;
    } else {
        return (forward.x > 0) ? EAST : WEST;
    }
}
                
void TurnRobotWithUserInputs(GameContext *ctx) {
    Direction camForward = GetCameraForwardDirection(ctx->camera);
    int baseDir = (int)camForward;

    if (IsKeyDown(KEY_W) || IsKeyDown(KEY_UP)) ctx->robot.direction = (Direction)((baseDir + 0) % 4);
    if (IsKeyDown(KEY_D) || IsKeyDown(KEY_RIGHT)) ctx->robot.direction = (Direction)((baseDir + 1) % 4);
    if (IsKeyDown(KEY_S) || IsKeyDown(KEY_DOWN)) ctx->robot.direction = (Direction)((baseDir + 2) % 4);
    if (IsKeyDown(KEY_A) || IsKeyDown(KEY_LEFT)) ctx->robot.direction = (Direction)((baseDir + 3) % 4);
}

//--------------------------------------------------------------------------------------
// State Functions
//--------------------------------------------------------------------------------------
void UpdateDrawMenu(GameContext *ctx) {
    // 1. HANDLE TEXT INPUT
    // GetCharPressed() returns the key code of the character pressed
    int key = GetCharPressed();

    // Check if more characters have been pressed on the same frame
    while (key > 0)
    {
        // Allow only alphanumeric characters (No spaces or commas to keep file IO simple)
        if ((key >= 32) && (key <= 125) && (ctx->usernameLen < 15) && isalnum(key))
        {
            ctx->username[ctx->usernameLen] = (char)key;
            ctx->username[ctx->usernameLen + 1] = '\0'; // Add null terminator
            ctx->usernameLen++;
        }
        key = GetCharPressed();  // Check next character in queue
    }

    if (IsKeyPressed(KEY_BACKSPACE))
    {
        if (ctx->usernameLen > 0)
        {
            ctx->usernameLen--;
            ctx->username[ctx->usernameLen] = '\0';
        }
    }

    // 2. START GAME LOGIC
    // Only allow start if they have typed a name
    if ((IsKeyPressed(KEY_ENTER) || IsKeyPressed(KEY_KP_ENTER)) && ctx->usernameLen > 0)
    {
        ctx->currentLevel = 0;
        ctx->score = 0;
        AdvanceLevel(ctx); 
        ctx->currentState = STATE_PLAYING;
    }

    // 3. DRAWING
    BeginDrawing();
        ClearBackground(RAYWHITE);
        int centerX = GetScreenWidth() / 2;

        // Title
        DrawText("ROBOT RESCUE", centerX - MeasureText("ROBOT RESCUE", 40)/2, 60, 40, DARKBLUE);

        // Input Box
        DrawText("ENTER USERNAME:", centerX - MeasureText("ENTER USERNAME:", 20)/2, 140, 20, DARKGRAY);
        
        // Draw the name or a blinking cursor
        char *displayName = TextFormat("%s_", ctx->username);
        DrawText(displayName, centerX - MeasureText(displayName, 30)/2, 170, 30, BLACK);
        
        if (ctx->usernameLen == 0) {
            DrawText("(Type name to start)", centerX - MeasureText("(Type name to start)", 15)/2, 205, 15, LIGHTGRAY);
        } else {
             DrawText("Press [ENTER] to Start", centerX - MeasureText("Press [ENTER] to Start", 20)/2, 205, 20, DARKGREEN);
        }

        // Instructions
        int instrY = 280;
        DrawText("--- INSTRUCTIONS ---", centerX - MeasureText("--- INSTRUCTIONS ---", 20)/2, instrY, 20, GRAY);
        
        instrY += 30;
        DrawText("CONTROLS: Use [WASD] or [ARROW KEYS] to move.", centerX - MeasureText("CONTROLS: Use [WASD] or [ARROW KEYS] to move.", 18)/2, instrY, 18, DARKGRAY);
        
        instrY += 25;
        const char* goal = "GOAL: Collect GREEN people. Avoid RED mines and Walls.";
        DrawText(goal, centerX - MeasureText(goal, 18)/2, instrY, 18, DARKGRAY);

        instrY += 25;
        DrawText("You have 5 batteries (lives). Good luck!", centerX - MeasureText("You have 5 batteries (lives). Good luck!", 18)/2, instrY, 18, DARKGRAY);

    EndDrawing();
}

void UpdateDrawGameplay(GameContext *ctx) {
    // Update
    if (IsKeyPressed(KEY_O)) ctx->orbitMode = !ctx->orbitMode;    

    // Pause and unpause logic
    if (IsKeyPressed(KEY_SPACE)) ctx->paused = !ctx->paused;

    // Change player mode logic
    if (IsKeyPressed(KEY_M)) ctx->aiModeEnabled = !ctx->aiModeEnabled;

    if (IsKeyPressed(KEY_PERIOD)) ctx->AStarHeuristicWeightage += 0.05f;
    if (IsKeyPressed(KEY_COMMA)) ctx->AStarHeuristicWeightage -= 0.05f;

    // For debugging
    //     if(IsKeyPressed(KEY_L)) ctx->livesRemaining += 1;
    //     if(IsKeyPressed(KEY_K)) ctx->livesRemaining += -1;
    //     printf("Lives remaining: %d", ctx->livesRemaining);
        // if(IsKeyPressed(KEY_L)) ctx->peopleRemaining += 1;
        // if(IsKeyPressed(KEY_K)) ctx->peopleRemaining += -1;
        // printf("People remaining: %d\n", ctx->peopleRemaining);
        if(IsKeyPressed(KEY_U)) AdvanceLevel(ctx);



    UpdateCustomCamera(&ctx->camera, &ctx->orbitMode);
    HandleGridInteraction(ctx);

    if (!ctx->paused) { // Gameplay: Inputs, entity movement, etc 
        ctx->frameCount++;
        // Move entities
            // People
            for (int i=0; i<NUM_PEOPLE; i++) {
                MoveMovingEntity(ctx, &ctx->people[i], CELL_PERSON);
            }
            // Mines
            for (int i=0; i<ctx->mineCount; i++) {
                MoveMovingEntity(ctx, &ctx->mines[i], CELL_MINE);
            }
        
        // Move robot
        // if user, take input every frame, but move robot after every cooldown
        // if ai, then run A* before every move, so both things have the cooldown
        if (!ctx->aiModeEnabled) TurnRobotWithUserInputs(ctx);

        int effectiveCooldown = ctx->robot.moveCooldown / (1 + IsKeyDown(KEY_LEFT_SHIFT));
        if (effectiveCooldown < 1) effectiveCooldown = 1;
        if (ctx->frameCount % effectiveCooldown == 0) {
            if (ctx->aiModeEnabled) move_robot_ai(ctx);
            // Move robot
            // ctx->robot.position;
            MoveEntity(ctx, (MovingEntity*)&ctx->robot, CELL_ROBOT, &ctx->robot.position, &ctx->robot.direction);
        }

        // check level advancement condition
        if (ctx->peopleRemaining <= 0) AdvanceLevel(ctx);
        // check death condition
        if (ctx->livesRemaining <= 0) ctx->currentState = STATE_GAME_OVER;
    }

    // Draw
    BeginDrawing();
        ClearBackground(RAYWHITE);
        
        DrawGameScene(ctx);

        if (ctx->paused) DrawText("Press [SPACE] to unpause", GetScreenWidth()/2 -170 , GetScreenHeight()/10, 29, DARKGRAY);
    EndDrawing();
}

void UpdateDrawGameOver(GameContext *ctx) {
    static bool isDataProcessed = false;
    static ScoreEntry topScores[MAX_LEADERBOARD_ENTRIES];
    static int totalScoresLoaded = 0;
    static int currentRunDuration = 0;

    // 1. ONE-TIME LOGIC (Save & Load)
    if (!isDataProcessed) {
        currentRunDuration = ctx->frameCount / 60;

        // A. APPEND CURRENT SCORE TO FILE (Format: Name,Level,Time)
        FILE *file = fopen("leaderboard.txt", "a");
        if (file != NULL) {
            // Default to "Unknown" if name somehow empty
            if (ctx->usernameLen == 0) strcpy(ctx->username, "Unknown");
            
            fprintf(file, "%s,%d,%d\n", ctx->username, ctx->currentLevel, currentRunDuration);
            fclose(file);
        }

        // B. READ ALL SCORES FROM FILE
        totalScoresLoaded = 0;
        file = fopen("leaderboard.txt", "r");
        if (file != NULL) {
            // Scan format: String(up to comma), Integer, Integer
            // %19[^,] means "Read up to 19 chars or until a comma is found"
            while (fscanf(file, "%19[^,],%d,%d\n", 
                   topScores[totalScoresLoaded].name, 
                   &topScores[totalScoresLoaded].level, 
                   &topScores[totalScoresLoaded].duration) == 3) 
            {
                totalScoresLoaded++;
                if (totalScoresLoaded >= MAX_LEADERBOARD_ENTRIES) break;
            }
            fclose(file);
        }

        // C. SORT THE SCORES
        if (totalScoresLoaded > 0) {
            qsort(topScores, totalScoresLoaded, sizeof(ScoreEntry), CompareScores);
        }

        isDataProcessed = true;
    }

    // 2. INPUT HANDLING
    if (IsKeyPressed(KEY_ENTER))
    {
        isDataProcessed = false; 
        
        // Reset Logic
        ctx->lastGridCellFocused = (Vector2){-1, -1};
        ctx->gridCellFocused = (Vector2){-1, -1};
        ctx->frameCount = 0;
        ctx->livesRemaining = 5;
        ctx->currentState = STATE_MENU;
    }

    // 3. DRAWING
    BeginDrawing();
        ClearBackground(BLACK);

        int centerX = GetScreenWidth() / 2;
        int y = 50;

        DrawText("GAME OVER", centerX - MeasureText("GAME OVER", 40)/2, y, 40, RED);
        y += 60;

        const char* scoreText = TextFormat("%s, you reached Level %d in %d seconds", ctx->username, ctx->currentLevel, currentRunDuration);
        DrawText(scoreText, centerX - MeasureText(scoreText, 20)/2, y, 20, YELLOW);
        y += 50;
        
        DrawText("--- LEADERBOARD ---", centerX - MeasureText("--- LEADERBOARD ---", 20)/2, y, 20, WHITE);
        y += 30;

        for (int i = 0; i < totalScoresLoaded && i < LEADERBOARD_DISPLAY_LIMIT; i++) {
            Color rowColor = (i == 0) ? GOLD : (i == 1) ? LIGHTGRAY : (i == 2) ? BROWN : GRAY;
            
            // Format: "1. Name - Lvl 5 - 40s"
            const char* entryText = TextFormat("%d. %s - Lvl %d - %ds", 
                i + 1, topScores[i].name, topScores[i].level, topScores[i].duration);
                
            DrawText(entryText, centerX - MeasureText(entryText, 20)/2, y, 20, rowColor);
            y += 30;
        }

        if (totalScoresLoaded == 0) {
            const char* noScores = "No previous scores found.";
            DrawText(noScores, centerX - MeasureText(noScores, 20)/2, y, 20, DARKGRAY);
        }

        const char* prompt = "Press [ENTER] to Return to Menu";
        DrawText(prompt, centerX - MeasureText(prompt, 20)/2, GetScreenHeight() - 50, 20, WHITE);

    EndDrawing();
}

//--------------------------------------------------------------------------------------
// Helper Implementations
//--------------------------------------------------------------------------------------

void InitGame(GameContext *ctx) {
    ctx->currentState = STATE_MENU;
    ctx->currentLevel = 0;
    ctx->score = 0;
    ctx->orbitMode = true;
    ctx->lastGridCellFocused = (Vector2){-1, -1};
    ctx->gridCellFocused = (Vector2){-1, -1};
    ctx->paused = true;
    ctx->mines = NULL;
    ctx->mines = malloc(sizeof(MovingEntity));
    if (ctx->mines == NULL) {
        printf("\nmalloc() failed. No free space in memory. Program exiting.\n");
        exit(EXIT_FAILURE);
    }
    ctx->mineCount = 0;
    ctx->robot.position = (Vector2){4, 4};
    ctx->robot.moveCooldown = 20;
    ctx->aiModeEnabled = true;
    ctx->livesRemaining = MAX_LIVES;

    ctx->peopleMaxMovesPerSec = 3.0f;
    ctx->minesMaxMovesPerSec = 5.0f;

    ctx->username[0] = '\0';
    ctx->usernameLen = 0;
    
    ctx->AStarHeuristicWeightage = 1.5f;

    // Setup Camera
    ctx->camera.position = (Vector3){ 0.0f, 20.0f, 20.0f };
    ctx->camera.target = (Vector3){ GRID_WIDTH*CELL_SIZE/2, 0.0f, GRID_HEIGHT*CELL_SIZE/2 };
    ctx->camera.up = (Vector3){ 0.0f, 1.0f, 0.0f };
    ctx->camera.fovy = 55.0f;
    ctx->camera.projection = CAMERA_PERSPECTIVE;

    // Init grid plus shape
    for (int i=4; i<GRID_WIDTH-4; i++) {ctx->grid[i][GRID_HEIGHT/2] = CELL_WALL;}
    for (int i=4; i<GRID_HEIGHT-4; i++) {ctx->grid[GRID_WIDTH/2][i] = CELL_WALL;}

    // Set people random movement speeds
    ctx->peopleRemaining = NUM_PEOPLE;
    for (int i=0; i<NUM_PEOPLE; i++) {
        ctx->people[i].liklihoodToMove = ctx->peopleMaxMovesPerSec * rand() / RAND_MAX / 60.0f;
        ctx->people[i].liklihoodToTurn = 0.5 * ctx->peopleMaxMovesPerSec * rand() / RAND_MAX / 60.0f;
    }
}

void AdvanceLevel(GameContext *ctx) {
    // Wipe the grid of people and mines
        for(int x=0; x<GRID_WIDTH; x++) {
            for(int y=0; y<GRID_HEIGHT; y++) {
                ctx->grid[x][y] = (ctx->grid[x][y] == CELL_WALL ? CELL_WALL : CELL_AIR);
            }
        }
        // Correspondingly, set the mines and persons positions to -1
            for (int i=0; i<NUM_PEOPLE; i++) {
                ctx->people[i].position = (Vector2){-1, -1};
            }
            for (int i=0; i<ctx->mineCount; i++) {
                ctx->mines[i].position = (Vector2){-1, -1};
            }
        
    ctx->currentLevel += 1;
    ctx->paused = true;
    ctx->robot.position = (Vector2){3*GRID_HEIGHT/4, GRID_WIDTH/4};
    ctx->grid[3*GRID_HEIGHT/4][GRID_WIDTH/4] = CELL_ROBOT;
    const int maxMines = 50;
    ctx->mineCount = min(ctx->currentLevel*5, maxMines);
    ctx->robot.moveCooldown = max(1, ctx->robot.moveCooldown - 1);
    if (ctx->robot.moveCooldown > 1) {
        ctx->robot.moveCooldown += - 1;
    } else {
        SetTargetFPS(max(60 + 10*(ctx->currentLevel - 30), 60));
    }
    
    ctx->mines = realloc(ctx->mines, sizeof(MovingEntity)*ctx->mineCount);
    if (ctx->mines == NULL) {
        printf("\nrealloc() failed. No free space in memory. Program exiting.\n");
        exit(EXIT_FAILURE);
    }

    
    // Spawn mines and people
        // Use level number as seed
        srand(ctx->currentLevel);

        // Place people
        ctx->peopleRemaining = 0;
        int max_attempts = 10;
        int x;
        int y;
        for (int i=0; i<NUM_PEOPLE; i++) {
            int attempt = 0;
            do {
                attempt++;
                x = rand() % GRID_WIDTH;
                y = rand() % GRID_HEIGHT;

                if (ctx->grid[x][y] != CELL_AIR) continue;
                ctx->people[i].position = (Vector2){x, y};
                ctx->people[i].direction = rand() % 4;
                ctx->grid[x][y] = CELL_PERSON;
                ctx->peopleRemaining += 1;
                break;
            } while (attempt < max_attempts);            
        }
        for (int i=0; i<ctx->mineCount; i++) {
            int attempt = 0;
            while (attempt < max_attempts) {
                attempt++;
                x = rand() % GRID_WIDTH;
                y = rand() % GRID_HEIGHT;

                if (ctx->grid[x][y] != CELL_AIR) continue;

                ctx->mines[i].position = (Vector2){x, y};
                ctx->mines[i].direction = rand() % 4;
                // Set mines random movement speeds
                ctx->mines[i].liklihoodToMove = ctx->minesMaxMovesPerSec * rand() / RAND_MAX / 60.0f;
                ctx->mines[i].liklihoodToTurn = 0.5f;
                ctx->grid[x][y] = CELL_MINE;

                break;
            }
            if (attempt >= max_attempts) { printf("Max spawn attempts exceeded."); }            
        }


}

//--------------------------------------------------------------------------------------
// Core Logic Functions
//--------------------------------------------------------------------------------------

void UpdateCustomCamera(Camera3D *camera, bool *orbitMode) {
    float panSensitivity = 0.1f; 

    // Panning (Middle Mouse)
    if (IsMouseButtonDown(MOUSE_BUTTON_MIDDLE))
    {
        Vector2 mouseDelta = GetMouseDelta();

        // Calculate vectors for flat ground movement
        Vector3 forward = Vector3Subtract(camera->target, camera->position);
        Vector3 right = Vector3CrossProduct(forward, (Vector3){ 0.0f, 1.0f, 0.0f });
        right = Vector3Normalize(right);

        Vector3 forwardGround = Vector3CrossProduct((Vector3){ 0.0f, 1.0f, 0.0f }, right);
        forwardGround = Vector3Normalize(forwardGround);

        // Apply movement to both position and target to keep view angle constant
        Vector3 move = Vector3Add(
            Vector3Scale(right, -mouseDelta.x * panSensitivity),
            Vector3Scale(forwardGround, mouseDelta.y * panSensitivity)
        );

        camera->position = Vector3Add(camera->position, move);
        camera->target = Vector3Add(camera->target, move);
    }
    // Orbiting (Standard Raylib Orbital Camera)
    else if (*orbitMode)
    {
        UpdateCamera(camera, CAMERA_ORBITAL);
    }
    // Zooming only (when Orbit is disabled)
    else
    {
        float wheel = GetMouseWheelMove();
        if (wheel != 0)
        {
            Vector3 move = Vector3Scale(Vector3Normalize(Vector3Subtract(camera->target, camera->position)), wheel * 2.0f);
            camera->position = Vector3Add(camera->position, move);
        }
    }
}

void HandleGridInteraction(GameContext *ctx) {
    Ray ray = GetScreenToWorldRay(GetMousePosition(), ctx->camera);

    // Ray-Plane Intersection (Ground is at Y=0, Normal is (0,1,0))
    // t = (center - ray.pos) . normal / (ray.dir . normal)
    float t = -ray.position.y / ray.direction.y;
    
    // Reset cursor validity
    ctx->gridCellFocused.x = -1;
    ctx->gridCellFocused.y = -1;

    // Check if we hit the floor (t >= 0 means hit in front of camera)
    if (t >= 0)
    {
        Vector3 hitPoint = Vector3Add(ray.position, Vector3Scale(ray.direction, t));

        int gridX = (int)(hitPoint.x / CELL_SIZE);
        int gridY = (int)(hitPoint.z / CELL_SIZE);

        // Check if inside Grid Boundaries
        if (gridX >= 0 && gridX < GRID_WIDTH && gridY >= 0 && gridY < GRID_HEIGHT)
        {
            ctx->gridCellFocused = (Vector2){ (float)gridX, (float)gridY };

            // Handle Painting
            if (ctx->aiModeEnabled && (IsMouseButtonDown(MOUSE_BUTTON_LEFT) || IsMouseButtonDown(MOUSE_BUTTON_RIGHT)))
            {
                int paintValue = IsMouseButtonDown(MOUSE_BUTTON_LEFT) ? CELL_WALL : CELL_AIR;

                // Interpolate line if we have a valid previous position to prevent gaps
                if (ctx->lastGridCellFocused.x != -1 && ctx->lastGridCellFocused.y != -1)
                {
                    PaintGridLine(ctx, (int)ctx->lastGridCellFocused.x, (int)ctx->lastGridCellFocused.y, gridX, gridY, paintValue);
                }
                else
                {
                    ctx->grid[gridX][gridY] = paintValue;
                }
            }
            
            ctx->lastGridCellFocused = ctx->gridCellFocused;
            return; // Exit early since we processed a valid hit
        }
    }

    // If we missed the floor or the grid, invalidate history
    ctx->lastGridCellFocused = (Vector2){ -1, -1 };
}

// This code is not mine. I took it from an example from the raylib github repo
static void DrawTextCodepoint3D(Font font, int codepoint, Vector3 position, float fontSize, bool backface, Color tint) {
    // Character index position in sprite font
    // NOTE: In case a codepoint is not available in the font, index returned points to '?'
    int index = GetGlyphIndex(font, codepoint);
    float scale = fontSize/(float)font.baseSize;

    // Character destination rectangle on screen
    // NOTE: We consider charsPadding on drawing
    position.x += (float)(font.glyphs[index].offsetX - font.glyphPadding)*scale;
    position.z += (float)(font.glyphs[index].offsetY - font.glyphPadding)*scale;

    // Character source rectangle from font texture atlas
    // NOTE: We consider chars padding when drawing, it could be required for outline/glow shader effects
    Rectangle srcRec = { font.recs[index].x - (float)font.glyphPadding, font.recs[index].y - (float)font.glyphPadding,
                        font.recs[index].width + 2.0f*font.glyphPadding, font.recs[index].height + 2.0f*font.glyphPadding };

    float width = (float)(font.recs[index].width + 2.0f*font.glyphPadding)*scale;
    float height = (float)(font.recs[index].height + 2.0f*font.glyphPadding)*scale;

    if (font.texture.id > 0)
    {
        const float x = 0.0f;
        const float y = 0.0f;
        const float z = 0.0f;

        // normalized texture coordinates of the glyph inside the font texture (0.0f -> 1.0f)
        const float tx = srcRec.x/font.texture.width;
        const float ty = srcRec.y/font.texture.height;
        const float tw = (srcRec.x+srcRec.width)/font.texture.width;
        const float th = (srcRec.y+srcRec.height)/font.texture.height;

        rlCheckRenderBatchLimit(4 + 4*backface);
        rlSetTexture(font.texture.id);

        rlPushMatrix();
            rlTranslatef(position.x, position.y, position.z);

            rlBegin(RL_QUADS);
                rlColor4ub(tint.r, tint.g, tint.b, tint.a);

                // Front Face
                rlNormal3f(0.0f, 1.0f, 0.0f);                                   // Normal Pointing Up
                rlTexCoord2f(tx, ty); rlVertex3f(x,         y, z);              // Top Left Of The Texture and Quad
                rlTexCoord2f(tx, th); rlVertex3f(x,         y, z + height);     // Bottom Left Of The Texture and Quad
                rlTexCoord2f(tw, th); rlVertex3f(x + width, y, z + height);     // Bottom Right Of The Texture and Quad
                rlTexCoord2f(tw, ty); rlVertex3f(x + width, y, z);              // Top Right Of The Texture and Quad

                if (backface)
                {
                    // Back Face
                    rlNormal3f(0.0f, -1.0f, 0.0f);                              // Normal Pointing Down
                    rlTexCoord2f(tx, ty); rlVertex3f(x,         y, z);          // Top Right Of The Texture and Quad
                    rlTexCoord2f(tw, ty); rlVertex3f(x + width, y, z);          // Top Left Of The Texture and Quad
                    rlTexCoord2f(tw, th); rlVertex3f(x + width, y, z + height); // Bottom Left Of The Texture and Quad
                    rlTexCoord2f(tx, th); rlVertex3f(x,         y, z + height); // Bottom Right Of The Texture and Quad
                }
            rlEnd();
        rlPopMatrix();

        rlSetTexture(0);
    }
}

// Draw a 2D text in 3D space
// This code is not mine. I took it from an example from the raylib github repo
static void DrawText3D(Font font, const char *text, Vector3 position, float fontSize, float fontSpacing, float lineSpacing, bool backface, Color tint) {
    int length = TextLength(text);          // Total length in bytes of the text, scanned by codepoints in loop
    
    float textOffsetY = 0.0f;               // Offset between lines (on line break '\n')
    float textOffsetX = 0.0f;               // Offset X to next character to draw

    float scale = fontSize/(float)font.baseSize;

    for (int i = 0; i < length;)
    {
        // Get next codepoint from byte string and glyph index in font
        int codepointByteCount = 0;
        int codepoint = GetCodepoint(&text[i], &codepointByteCount);
        int index = GetGlyphIndex(font, codepoint);

        // NOTE: Normally we exit the decoding sequence as soon as a bad byte is found (and return 0x3f)
        // but we need to draw all of the bad bytes using the '?' symbol moving one byte
        if (codepoint == 0x3f) codepointByteCount = 1;

        if (codepoint == '\n')
        {
            textOffsetY += fontSize + lineSpacing;
            textOffsetX = 0.0f;
        }
        else
        {
            if ((codepoint != ' ') && (codepoint != '\t'))
            {
                DrawTextCodepoint3D(font, codepoint, (Vector3){ position.x + textOffsetX, position.y, position.z + textOffsetY }, fontSize, backface, tint);
            }

            if (font.glyphs[index].advanceX == 0) textOffsetX += (float)font.recs[index].width*scale + fontSpacing;
            else textOffsetX += (float)font.glyphs[index].advanceX*scale + fontSpacing;
        }

        i += codepointByteCount;   // Move text bytes counter to next codepoint
    }
}

void Draw3DHUD(GameContext *ctx) {
    float worldWidth = GRID_WIDTH * CELL_SIZE;
    float worldHeight = GRID_HEIGHT * CELL_SIZE;
    
    // Config
    float fontSize = 3.0f;
    float spacing = 0.1f;
    Font font = GetFontDefault();
    float fontScale = fontSize / (float)font.baseSize; // Calculate scale to convert 2D measure to 3D

    // Height offset to prevent Z-fighting (clipping into the floor)
    float hoverHeight = 0.05f; 

    // --- NORTH EDGE (Controls) ---
    rlPushMatrix();
        const char* txtNorth = "L-Click : Paint | R-Click : Erase | M-Click : Pan | O : Orbit\n Space : Pause | L-Shift : Sprint | </> : change A* Heuristic weighting";
        // Measure exact width
        float widthN = MeasureTextEx(font, txtNorth, (float)font.baseSize, 1.0f).x * fontScale;
        
        // Position: Top Center, slightly outside (-Z)
        float northZ = -4.0f;
        rlTranslatef((worldWidth/2) - (widthN/2), hoverHeight, northZ);
        
        // Orientation
        rlRotatef(90.0f, 1.0f, 0.0f, 0.0f); // Lay flat
        // Flip if camera is "Inside" the grid relative to this text
        if (ctx->camera.position.z < northZ) {
            rlTranslatef(widthN/2, 0, 0);       // Move to center
            rlRotatef(180.0f, 0.0f, 0.0f, 1.0f);// Spin 180 (Around Z local is Y global after X rot)
            rlTranslatef(-widthN/2, 0, 0);      // Move back
        }

        DrawText3D(font, txtNorth, (Vector3){0,0,-6.0f}, fontSize, spacing, 0, true, DARKGRAY);
    rlPopMatrix();


    // --- SOUTH EDGE (Level & People) ---
    rlPushMatrix();
        const char* txtSouth = TextFormat("Level: %d  |  People: %d", ctx->currentLevel, ctx->peopleRemaining);
        float widthS = MeasureTextEx(font, txtSouth, (float)font.baseSize, 1.0f).x * fontScale;
        
        // Position: Bottom Center, slightly outside (+Z)
        float southZ = worldHeight + 4.0f;
        rlTranslatef((worldWidth/2) - (widthS/2), hoverHeight, southZ);
        
        // Orientation
        rlRotatef(90.0f, 1.0f, 0.0f, 0.0f);
        // Flip if camera is "Outside" the grid (South of text)
        if (ctx->camera.position.z < southZ) {
            rlTranslatef(widthS/2, 0, 0);
            rlRotatef(180.0f, 0.0f, 0.0f, 1.0f);
            rlTranslatef(-widthS/2, 0, 0); 
        }

        DrawText3D(font, txtSouth, (Vector3){0,0,-2.5f}, fontSize, spacing, 0, true, BLACK);
    rlPopMatrix();


    // --- WEST EDGE (Mode) ---
    rlPushMatrix();
        const char* txtWest = TextFormat("Mode: %s\nA* Heuristic weighting: %.2f", ctx->aiModeEnabled ? "AI" : "MANUAL", ctx->AStarHeuristicWeightage);
        float widthW = MeasureTextEx(font, txtWest, (float)font.baseSize, 1.0f).x * fontScale;

        // Position: Left Center, outside (-X)
        float westX = -4.0f;
        // Note: For side text, we translate to the center point first to make rotation math easier
        rlTranslatef(westX, hoverHeight, (worldHeight/2) + (widthW/2));
        
        // Orientation
        rlRotatef(90.0f, 1.0f, 0.0f, 0.0f); // Lay flat
        rlRotatef(-90.0f, 0.0f, 0.0f, 1.0f); // Rotate to face West
        
        // Flip logic based on X axis
        if (ctx->camera.position.x < westX) {
             rlTranslatef(widthW/2, 0, 0);
             rlRotatef(180.0f, 0.0f, 0.0f, 1.0f);
             rlTranslatef(-widthW/2, 0, 0);
        }

        DrawText3D(font, txtWest, (Vector3){0,0,-6.0f}, fontSize, spacing, 0, true, BLUE);
    rlPopMatrix();


    // --- EAST EDGE (Status & FPS) ---
    rlPushMatrix();
        const char* txtEast = ctx->paused ? "[ PAUSED ]" : (IsKeyDown(KEY_O) ? "Orbiting..." : "Running");
        const char* txtFPS = TextFormat("FPS: %i", GetFPS());

        float widthE = MeasureTextEx(font, txtEast, (float)font.baseSize, 1.0f).x * fontScale;
        float widthFPS = MeasureTextEx(font, txtFPS, (float)font.baseSize, 1.0f).x * fontScale;

        // Determine the widest line to center the block visually
        float maxWidth = (widthE > widthFPS) ? widthE : widthFPS;

        // Position: Right Center, outside (+X)
        float eastX = worldWidth + 4.0f;
        rlTranslatef(eastX, hoverHeight, (worldHeight/2) - (maxWidth/2));

        // Orientation
        rlRotatef(90.0f, 1.0f, 0.0f, 0.0f); // Lay flat
        rlRotatef(90.0f, 0.0f, 0.0f, 1.0f); // Rotate to face East

        // Flip logic
        if (ctx->camera.position.x > eastX) {
             rlTranslatef(maxWidth/2, 0, 0); // Rotate around center of the block
             rlRotatef(180.0f, 0.0f, 0.0f, 1.0f);
             rlTranslatef(-maxWidth/2, 0, 0);
        }

        // Draw Status (Centered in the block)
        float offsetE = (maxWidth - widthE) / 2.0f;
        DrawText3D(font, txtEast, (Vector3){offsetE, 0, -5.5f}, fontSize, spacing, 0, true, ctx->paused ? RED : DARKGRAY);
        
        // Draw FPS (Below status, Centered in the block)
        float offsetFPS = (maxWidth - widthFPS) / 2.0f;
        DrawText3D(font, txtFPS, (Vector3){offsetFPS, 0, -3.0f}, fontSize, spacing, 0, true, LIME);
    rlPopMatrix();
}

// Draws a single battery at a specific location and rotation
void DrawSingleBattery(Vector3 pos, float rotationY, bool isActive) {
    // Dimensions
    float h = 9.0f;       
    float r = 2.3f;       
    int slices = 16;      
    
    // Colors
    Color bodyColor  = isActive ? (Color){ 0, 228, 48, 255 } : (Color){ 80, 0, 0, 255 }; 
    Color wireColor  = isActive ? (Color){ 0, 100, 0, 255 }  : (Color){ 60, 0, 0, 255 };
    Color capColor   = isActive ? (Color){ 40, 40, 40, 255 } : (Color){ 60, 50, 40, 255 };

    rlPushMatrix();
        rlTranslatef(pos.x, pos.y, pos.z);
        rlRotatef(rotationY, 0, 1, 0); 

        // 1. Bottom Cap
        DrawCylinder((Vector3){0, 0.25f, 0}, r, r, 0.5f, slices, capColor);
        DrawCylinderWires((Vector3){0, 0.25f, 0}, r, r, 0.5f, slices, BLACK);

        // 2. Main Energy Body
        // We explicitly set the position to sit directly on the bottom cap (0.5f height)
        float startHeight = -4.0f;
        float gapHeight = h - 1.0f;
        // Extend slightly into caps to prevent floating
        float bodyHeight = gapHeight + 0.4f; 
        
        // Position center at: startHeight + half the body height - slight offset to clip down
        Vector3 bodyPos = {0, startHeight + bodyHeight/2.0f - 0.1f, 0};
        
        DrawCylinder(bodyPos, r*0.85f, r*0.85f, bodyHeight, slices, bodyColor);
        DrawCylinderWires(bodyPos, r*0.85f, r*0.85f, bodyHeight, slices, wireColor);

        // 3. Top Cap
        // Draw this BEFORE the transparent glow to ensure it renders behind the transparency correctly
        DrawCylinder((Vector3){0, h - 0.25f, 0}, r, r, 0.5f, slices, capColor);
        DrawCylinderWires((Vector3){0, h - 0.25f, 0}, r, r, 0.5f, slices, BLACK);

        // 4. Positive Terminal
        DrawCylinder((Vector3){0, h + 0.2f, 0}, r*0.3f, r*0.3f, 0.4f, 8, capColor);

        // 5. GLOW EFFECT (Only when active)
        if (isActive) {
            rlDisableDepthMask();
            
            // Inner strong glow (3x wider than before)
            DrawCylinder(bodyPos, r*3.0f, r*3.0f, bodyHeight*0.9f, slices, (Color){ 0, 255, 0, 40 });
            // Outer soft halo
            DrawCylinder(bodyPos, r*4.5f, r*4.5f, bodyHeight*0.8f, slices, (Color){ 0, 255, 0, 20 });
            
            rlEnableDepthMask();
        }

    rlPopMatrix();
}

// Calculates pentagon positions on the fly and draws the batteries
void DrawBatteries(GameContext *ctx) {
    float centerX = (GRID_WIDTH * CELL_SIZE) / 2.0f;
    float centerZ = (GRID_HEIGHT * CELL_SIZE) / 2.0f;
    
    // Angle between batteries (360 / 5 = 72 degrees)
    float angleStep = 2.0f * PI / MAX_LIVES;
    
    // Start at 90 degrees (Top/North) 
    // We add PI because in Raylib 3D, Z+ is down, Z- is up. 
    float startAngle = PI; 

    for (int i = 0; i < MAX_LIVES; i++) {
        float angle = startAngle + (i * angleStep);

        // 1. Calculate Position
        Vector3 pos;
        pos.x = centerX + sinf(angle) * BATTERY_RADIUS;
        pos.y = 0.0f; // On floor
        pos.z = centerZ + cosf(angle) * BATTERY_RADIUS;

        // 2. Calculate Rotation
        // We convert the placement angle to degrees.
        // We rotate it so the "front" of the battery faces the center.
        float rotationDeg = (angle * RAD2DEG);

        // 3. Determine State
        // If i is less than lives remaining, it's charged.
        // Example: 3 Lives -> Indices 0, 1, 2 are Active. 3, 4 are Empty.
        bool isActive = (i < ctx->livesRemaining);

        // 4. Draw
        DrawSingleBattery(pos, rotationDeg, isActive);
    }
}


void DrawGameScene(GameContext *ctx) {
    void Draw3DInfo(MovingEntity *entity) {
        rlPushMatrix();
            // this rotates the reference frame around an axis given.
            // parameters seem to be (angle, x, y, z)
            // rotate to be horizontal
            rlRotatef(180.0f, 1.0f, 0.0f, 0.0f);
            char *txt = (char *)TextFormat(">");
            Vector3 pos = { -entity->position.x*CELL_SIZE - 0.6*CELL_SIZE, CELL_SIZE/2 + 0.01f, -entity->position.y*CELL_SIZE - 1.4*CELL_SIZE};
            DrawText3D(GetFontDefault(), txt, pos, 4.0f, 0.1f, 0.0f, true, BLACK);
        rlPopMatrix();
    }

    void DrawDirectionalEyes(MovingEntity *entity) {
        // 1. Define constants to remove magic numbers
        float eyeSize = CELL_SIZE / 3.0f;
        float pupilSize = CELL_SIZE / 6.0f;
        float eyeHeight = CELL_SIZE * 0.375f;   // 3/8
        float pupilHeight = CELL_SIZE * 0.48f; 
        float offset = CELL_SIZE * 0.375f;      // How far out/forward the eyes are
        float pupilOffset = offset + (eyeSize - pupilSize)/2 + 0.06f; 
        // 2. Calculate World Position of the entity center
        Vector3 centerPos = {
            (entity->position.x * CELL_SIZE) + CELL_SIZE/2, 
            0.0f, 
            (entity->position.y * CELL_SIZE) + CELL_SIZE/2
        };

        // 3. Convert Direction Enum (0-3) to Degrees (0, -90, -180, -270)
        // We multiply by -90 because Raylib's 3D rotation usually goes counter-clockwise
        float rotationAngle = entity->direction * -90.0f; 

        // 4. Matrix Transformation
        rlPushMatrix();
            // Move to the robot's center
            rlTranslatef(centerPos.x, 0.0f, centerPos.z);
            // Rotate the entire coordinate system to match the robot's facing
            rlRotatef(rotationAngle, 0.0f, 1.0f, 0.0f);

            // --- DRAWING IN LOCAL SPACE ---
            // Now, +Z is always "Forward" and +X is always "Right" relative to the robot.
            
            // Right Eye (White) - Located at (+Offset, Height, -Offset)
            DrawCube((Vector3){ offset, eyeHeight, -offset }, eyeSize, eyeSize, eyeSize, WHITE);
            // Left Eye (White) - Located at (-Offset, Height, -Offset)
            DrawCube((Vector3){ -offset, eyeHeight, -offset }, eyeSize, eyeSize, eyeSize, WHITE);

            // Right Pupil (Black) - Slightly further out/up to avoid Z-fighting
            DrawCube((Vector3){ pupilOffset, pupilHeight, -pupilOffset }, pupilSize, pupilSize, pupilSize, BLACK);
            // Left Pupil (Black)
            DrawCube((Vector3){ -pupilOffset, pupilHeight, -pupilOffset}, pupilSize, pupilSize, pupilSize, BLACK);

        rlPopMatrix();
    }


    BeginMode3D(ctx->camera);

    // Draw A* path
    if (ctx->aiModeEnabled && ctx->currentPathLen > 0) {
        // Draw line from robot to first node
        Vector3 start = { 
            (ctx->robot.position.x * CELL_SIZE) + CELL_SIZE/2, 
            0.5f, 
            (ctx->robot.position.y * CELL_SIZE) + CELL_SIZE/2 
        };
        
        // Draw the rest of the path
        for (int i = ctx->currentPathLen - 1; i >= 0; i--) {
            Vector3 end = { 
                (ctx->currentPath[i].x * CELL_SIZE) + CELL_SIZE/2, 
                0.5f, 
                (ctx->currentPath[i].y * CELL_SIZE) + CELL_SIZE/2 
            };
            
            DrawLine3D(start, end, RED);
            DrawCube(end, 0.5f, 0.5f, 0.5f, RED); // Small node marker
            start = end; // Move start to current for next segment
        }
    }
    
    // Draw UI text at the edges of the grid
    Draw3DHUD(ctx);

    // Draw the dynamic batteries at points of a pentagon
    // On when life is still available
    DrawBatteries(ctx);

    for (int x = 0; x < GRID_WIDTH; x++)
    {
        for (int y = 0; y < GRID_HEIGHT; y++)
        {
            Vector3 cellPos = {
                (x * CELL_SIZE) + CELL_SIZE/2, 
                0.0f, 
                (y * CELL_SIZE) + CELL_SIZE/2
            };

            if (ctx->grid[x][y] != CELL_AIR)
            {
                DrawCube(cellPos, CELL_SIZE, CELL_SIZE, CELL_SIZE, cellFillColours[ctx->grid[x][y]-1]);
                DrawCubeWires(cellPos, CELL_SIZE, CELL_SIZE, CELL_SIZE, cellOutlineColours[ctx->grid[x][y]-1]);
            }
            else
            {
                // Draw faint floor outline
                cellPos.y = -CELL_SIZE/2;
                DrawCubeWires(cellPos, CELL_SIZE, 0.0f, CELL_SIZE, LIGHTGRAY);
            }
        }
    }

    // Draw directional eyes
        // People
        for (int i=0; i<NUM_PEOPLE; i++) {
            if (ctx->people[i].position.x == -1) continue;
            DrawDirectionalEyes(&ctx->people[i]);
        }
        // Robot
        DrawDirectionalEyes((MovingEntity*)&ctx->robot);

    // Draw Cursor Highlight
    if (ctx->gridCellFocused.x != -1 && ctx->gridCellFocused.y != -1)
    {
        Vector3 highlightPos = {
            (ctx->gridCellFocused.x * CELL_SIZE) + CELL_SIZE/2, -CELL_SIZE/4, 
            (ctx->gridCellFocused.y * CELL_SIZE) + CELL_SIZE/2
        };
        DrawCube(highlightPos, CELL_SIZE, 1.0f, CELL_SIZE, Fade(GRAY, 0.5f));
        DrawCubeWires(highlightPos, CELL_SIZE, 1.0f, CELL_SIZE, DARKGRAY);
    }


    EndMode3D();
}

// Uses Bresenham's Line Algorithm to paint a continuous line of cells
void PaintGridLine(GameContext *ctx, int x0, int y0, int x1, int y1, int value) {
    int dx = abs(x1 - x0);
    int dy = abs(y1 - y0);
    int sx = (x0 < x1) ? 1 : -1;
    int sy = (y0 < y1) ? 1 : -1;
    int err = dx - dy;

    while (true)
    {
        if (ctx->grid[x0][y0] <= 1 && x0 >= 0 && x0 < GRID_WIDTH && y0 >= 0 && y0 < GRID_HEIGHT)
        {
            ctx->grid[x0][y0] = value;
        }

        if (x0 == x1 && y0 == y1) break;
        
        int e2 = 2 * err;
        if (e2 > -dy) { err -= dy; x0 += sx; }
        if (e2 < dx) { err += dx; y0 += sy; }
    }
}

int min(int a, int b) {
    return a < b ? a : b;
}

int max(int a, int b) {
    return a > b ? a : b;
}

int CompareScores(const void *a, const void *b) {
    ScoreEntry *entryA = (ScoreEntry *)a;
    ScoreEntry *entryB = (ScoreEntry *)b;

    // Higher level is better
    if (entryB->level != entryA->level) {
        return entryB->level - entryA->level;
    }
    // If levels are equal, shorter duration is better (survival)
    return entryA->duration - entryB->duration;
}