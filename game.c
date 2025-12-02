#include "raylib.h"
#include "rlgl.h"
#include "raymath.h"
#include <stdlib.h>
#include <stdio.h> // For sprintf

//--------------------------------------------------------------------------------------
// Constants & Definitions
//--------------------------------------------------------------------------------------
    #define GRID_WIDTH 30
    #define GRID_HEIGHT 30
    #define CELL_SIZE 2.0f
    #define MAX_LIVES 5
    #define NUM_PEOPLE 5
    #define BATTERY_RADIUS (GRID_WIDTH * CELL_SIZE * 0.8f)    

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

    int min(int a, int b);

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

void MoveEntity(GameContext *ctx, MovingEntity *entity, CellType entityCellType, Vector2 *pos, Direction *dir, bool isRobot) {
    Vector2* dirVec = &DIR_VECTORS[*dir];
    Vector2 futurePos = Vector2Add(*pos, *dirVec); 
    // check its not outside the grid
    if (   futurePos.x > GRID_WIDTH  || futurePos.x <= 0
        || futurePos.y > GRID_HEIGHT || futurePos.y <= 0) return;
    

    CellType futureCell = ctx->grid[(int)futurePos.x][(int)futurePos.y];
    // if robot collides with person
    if (futureCell == CELL_PERSON) {
        // only robots can interact
        if (!isRobot) return;

        ctx->peopleRemaining =+ -1;
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

    if (futureCell == CELL_WALL || futureCell == CELL_MINE) {
        if (isRobot) {
            ctx->livesRemaining =+ -1;
            // reset pos
            ctx->robot.position = (Vector2){3*GRID_HEIGHT/4, GRID_WIDTH/4};
        }
        return;
    }

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
        MoveEntity(ctx, entity, entityCellType, &entity->position, &entity->direction, false);
    }
}

void TurnRobotWithAI(GameContext *ctx) {
    // Calculate Weighted A* Algo path
    // Store, so we can display to user
    
    // Turn in direction necessary to reach next node in path

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
    // Update
    if (IsKeyPressed(KEY_ENTER) || IsMouseButtonPressed(MOUSE_BUTTON_LEFT) || IsKeyPressed(KEY_SPACE))
    {
        ctx->currentLevel = 0;
        ctx->score = 0;
        AdvanceLevel(ctx); // Clear grid and setup level 1
        ctx->currentState = STATE_PLAYING;
    }

    // Draw
    BeginDrawing();
        ClearBackground(RAYWHITE);
        DrawText("ROBOT RESCUE", GetScreenWidth()/2 - 150, GetScreenHeight()/4, 40, DARKBLUE);
        DrawText("Press [SPACE] to Start Level 1", GetScreenWidth()/2 - 140, GetScreenHeight()/4 + 50, 20, DARKGRAY);
    EndDrawing();
}

void UpdateDrawGameplay(GameContext *ctx) {
    ctx->frameCount++;

    // Update
    if (IsKeyPressed(KEY_O)) ctx->orbitMode = !ctx->orbitMode;
    
    // Toggle state to Game Over (Temporary - press G to die)
    if (IsKeyPressed(KEY_G)) ctx->currentState = STATE_GAME_OVER;

    // Pause and unpause logic
    if (IsKeyPressed(KEY_SPACE)) ctx->paused = !ctx->paused;

    // Change player mode logic
    if (IsKeyPressed(KEY_M)) ctx->aiModeEnabled = !ctx->aiModeEnabled;

    // For debugging
        // if(IsKeyPressed(KEY_L)) ctx->livesRemaining += 1;
        // if(IsKeyPressed(KEY_K)) ctx->livesRemaining += -1;


    UpdateCustomCamera(&ctx->camera, &ctx->orbitMode);
    HandleGridInteraction(ctx);

    if (!ctx->paused)
    { // Gameplay: Inputs, entity movement, etc 
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

        if (ctx->frameCount % (ctx->robot.moveCooldown)/(1+IsKeyDown(KEY_LEFT_SHIFT)) == 0) {
            if (ctx->aiModeEnabled) { TurnRobotWithAI(ctx);}
            // Move robot
            // ctx->robot.position;
            MoveEntity(ctx, (MovingEntity*)&ctx->robot, CELL_ROBOT, &ctx->robot.position, &ctx->robot.direction, true);
        }

    }

    // Draw
    BeginDrawing();
        ClearBackground(RAYWHITE);
        
        DrawGameScene(ctx);

        if (ctx->paused) DrawText("Press [SPACE] to unpause", GetScreenWidth()/2 -170 , GetScreenHeight()/10, 29, DARKGRAY);
    EndDrawing();
}

void UpdateDrawGameOver(GameContext *ctx) {
    // Update
    if (IsKeyPressed(KEY_ENTER))
    {
        // Restart Game
            ctx->currentState = STATE_MENU;
            ctx->currentLevel = 1;
            ctx->score = 0;
            ctx->orbitMode = true;
            ctx->lastGridCellFocused = (Vector2){-1, -1};
            ctx->gridCellFocused = (Vector2){-1, -1};
        ctx->currentState = STATE_MENU;
    }

    // Draw
    BeginDrawing();
        ClearBackground(BLACK);
        DrawText("GAME OVER", 300, 100, 40, RED);
        
        DrawText("Leaderboard Placeholder:", 300, 180, 20, WHITE);
        DrawText("1. AAA - 500", 300, 210, 20, GRAY);
        DrawText("2. BBB - 300", 300, 240, 20, GRAY);
        DrawText("3. CCC - 100", 300, 270, 20, GRAY);

        DrawText("Press [ENTER] to Return to Menu", 240, 350, 20, WHITE);
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
    
    // 
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
        const char* txtNorth = "L-Click: Paint | R-Click: Erase | M-Click: Pan | Space: Pause";
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

        DrawText3D(font, txtNorth, (Vector3){0,0,-1.0f}, fontSize, spacing, 0, true, DARKGRAY);
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

        DrawText3D(font, txtSouth, (Vector3){0,0,-1.0f}, fontSize, spacing, 0, true, BLACK);
    rlPopMatrix();


    // --- WEST EDGE (Mode) ---
    rlPushMatrix();
        const char* txtWest = TextFormat("Mode: %s", ctx->aiModeEnabled ? "AI" : "MANUAL");
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

        DrawText3D(font, txtWest, (Vector3){0,0,-1.0f}, fontSize, spacing, 0, true, BLUE);
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