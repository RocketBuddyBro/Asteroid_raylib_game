#include "raylib.h"
#include <math.h>
#include <stdbool.h>
#include <string.h>
#include <time.h>

#define SCREEN_WIDTH 1200
#define SCREEN_HEIGHT 760
#define STAR_COUNT 150
#define MAX_ASTEROIDS 96
#define MAX_BULLETS 80
#define MAX_PARTICLES 520
#define ASTEROID_VERTICES 10
#define DEG_TO_RAD 0.01745329251994329577f

typedef enum GameScreen {
    SCREEN_TITLE,
    SCREEN_PLAYING,
    SCREEN_GAME_OVER
} GameScreen;

typedef struct Star {
    Vector2 position;
    float radius;
    float phase;
} Star;

typedef struct Ship {
    Vector2 position;
    Vector2 velocity;
    float rotation;          // Degrees; -90 points upward.
    float invulnerability;
    bool thrusting;
} Ship;

typedef struct Asteroid {
    bool active;
    Vector2 position;
    Vector2 velocity;
    float radius;
    float rotation;
    float rotationSpeed;
    float shape[ASTEROID_VERTICES];
} Asteroid;

typedef struct Bullet {
    bool active;
    Vector2 position;
    Vector2 velocity;
    float life;
} Bullet;

typedef struct Particle {
    bool active;
    Vector2 position;
    Vector2 velocity;
    float life;
    float maxLife;
    float size;
    Color color;
} Particle;

static const Color COLOR_BACKGROUND = { 7, 10, 24, 255 };
static const Color COLOR_CYAN = { 88, 236, 255, 255 };
static const Color COLOR_BLUE = { 92, 150, 255, 255 };
static const Color COLOR_ORANGE = { 255, 145, 82, 255 };
static const Color COLOR_YELLOW = { 255, 218, 120, 255 };
static const Color COLOR_ASTEROID = { 154, 193, 215, 255 };
static const Color COLOR_MUTED = { 126, 151, 180, 255 };
static const Color COLOR_PANEL = { 12, 19, 39, 255 };

static Star stars[STAR_COUNT];
static Ship ship;
static Asteroid asteroids[MAX_ASTEROIDS];
static Bullet bullets[MAX_BULLETS];
static Particle particles[MAX_PARTICLES];
static GameScreen gameScreen = SCREEN_TITLE;
static int score = 0;
static int bestScore = 0;
static int lives = 3;
static int wave = 1;
static float shootCooldown = 0.0f;

static Vector2 Vec2(float x, float y)
{
    Vector2 result = { x, y };
    return result;
}

static Vector2 Add(Vector2 a, Vector2 b)
{
    return Vec2(a.x + b.x, a.y + b.y);
}

static Vector2 Subtract(Vector2 a, Vector2 b)
{
    return Vec2(a.x - b.x, a.y - b.y);
}

static Vector2 Scale(Vector2 value, float amount)
{
    return Vec2(value.x * amount, value.y * amount);
}

static float LengthSquared(Vector2 value)
{
    return value.x * value.x + value.y * value.y;
}

static float DistanceSquared(Vector2 a, Vector2 b)
{
    return LengthSquared(Subtract(a, b));
}

static Vector2 DirectionFromAngle(float degrees)
{
    float radians = degrees * DEG_TO_RAD;
    return Vec2(cosf(radians), sinf(radians));
}

static Color WithAlpha(Color color, float alpha)
{
    if (alpha < 0.0f) alpha = 0.0f;
    if (alpha > 1.0f) alpha = 1.0f;
    color.a = (unsigned char)(255.0f * alpha);
    return color;
}

static void WrapPosition(Vector2 *position)
{
    if (position->x < 0.0f) position->x += (float)SCREEN_WIDTH;
    else if (position->x >= (float)SCREEN_WIDTH) position->x -= (float)SCREEN_WIDTH;

    if (position->y < 0.0f) position->y += (float)SCREEN_HEIGHT;
    else if (position->y >= (float)SCREEN_HEIGHT) position->y -= (float)SCREEN_HEIGHT;
}

static void InitializeStars(void)
{
    for (int i = 0; i < STAR_COUNT; i++) {
        stars[i].position = Vec2((float)GetRandomValue(0, SCREEN_WIDTH),
                                 (float)GetRandomValue(0, SCREEN_HEIGHT));
        stars[i].radius = (float)GetRandomValue(1, 2);
        stars[i].phase = (float)GetRandomValue(0, 628) / 100.0f;
    }
}

static void DrawStarfield(void)
{
    float time = (float)GetTime();

    for (int i = 0; i < STAR_COUNT; i++) {
        float twinkle = 0.24f + 0.48f * (0.5f + 0.5f * sinf(time * 1.8f + stars[i].phase));
        DrawCircleV(stars[i].position, stars[i].radius,
                    WithAlpha((Color){ 176, 213, 255, 255 }, twinkle));
    }

    // Subtle decorative corner marks give the screen a cockpit-like feel.
    Color guide = WithAlpha(COLOR_BLUE, 0.16f);
    DrawLineEx(Vec2(22, 96), Vec2(22, 130), 1.0f, guide);
    DrawLineEx(Vec2(22, 96), Vec2(58, 96), 1.0f, guide);
    DrawLineEx(Vec2(SCREEN_WIDTH - 22, 96), Vec2(SCREEN_WIDTH - 22, 130), 1.0f, guide);
    DrawLineEx(Vec2(SCREEN_WIDTH - 22, 96), Vec2(SCREEN_WIDTH - 58, 96), 1.0f, guide);
}

static void SpawnParticle(Vector2 position, Vector2 velocity, float life,
                          Color color, float size)
{
    for (int i = 0; i < MAX_PARTICLES; i++) {
        if (!particles[i].active) {
            particles[i].active = true;
            particles[i].position = position;
            particles[i].velocity = velocity;
            particles[i].life = life;
            particles[i].maxLife = life;
            particles[i].color = color;
            particles[i].size = size;
            return;
        }
    }
}

static void SpawnExplosion(Vector2 position, int amount, Color mainColor)
{
    for (int i = 0; i < amount; i++) {
        float angle = (float)GetRandomValue(0, 359);
        float speed = (float)GetRandomValue(35, 230);
        float life = (float)GetRandomValue(25, 85) / 100.0f;
        Color color = mainColor;
        int choice = GetRandomValue(0, 3);
        if (choice == 0) color = COLOR_ORANGE;
        else if (choice == 1) color = COLOR_YELLOW;
        else if (choice == 2) color = COLOR_CYAN;

        SpawnParticle(position, Scale(DirectionFromAngle(angle), speed), life,
                      color, (float)GetRandomValue(1, 4));
    }
}

static void SpawnThrustParticle(void)
{
    float angle = ship.rotation + 180.0f + (float)GetRandomValue(-22, 22);
    float speed = (float)GetRandomValue(80, 190);
    Vector2 backward = DirectionFromAngle(angle);
    Vector2 position = Add(ship.position, Scale(DirectionFromAngle(ship.rotation), -13.0f));
    Vector2 velocity = Add(Scale(backward, speed), Scale(ship.velocity, 0.18f));
    Color color = (GetRandomValue(0, 1) == 0) ? COLOR_ORANGE : COLOR_CYAN;
    SpawnParticle(position, velocity, (float)GetRandomValue(12, 28) / 100.0f,
                  color, (float)GetRandomValue(2, 4));
}

static void SpawnAsteroid(Vector2 position, float radius)
{
    for (int i = 0; i < MAX_ASTEROIDS; i++) {
        if (!asteroids[i].active) {
            Asteroid *asteroid = &asteroids[i];
            asteroid->active = true;
            asteroid->position = position;
            asteroid->radius = radius;
            asteroid->rotation = (float)GetRandomValue(0, 359);
            asteroid->rotationSpeed = (float)GetRandomValue(-55, 55);

            float angle = (float)GetRandomValue(-180, 180);
            float speed = (float)GetRandomValue(45, 105) + (48.0f - radius) * 1.2f;
            asteroid->velocity = Scale(DirectionFromAngle(angle), speed);

            for (int p = 0; p < ASTEROID_VERTICES; p++) {
                asteroid->shape[p] = (float)GetRandomValue(77, 122) / 100.0f;
            }
            return;
        }
    }
}

static void SpawnWave(int waveNumber)
{
    int count = 4 + waveNumber;
    if (count > 12) count = 12;

    for (int i = 0; i < count; i++) {
        Vector2 position;
        int edge = GetRandomValue(0, 3);

        if (edge == 0) position = Vec2(0.0f, (float)GetRandomValue(100, SCREEN_HEIGHT - 1));
        else if (edge == 1) position = Vec2((float)SCREEN_WIDTH - 1.0f,
                                            (float)GetRandomValue(100, SCREEN_HEIGHT - 1));
        else if (edge == 2) position = Vec2((float)GetRandomValue(0, SCREEN_WIDTH), 90.0f);
        else position = Vec2((float)GetRandomValue(0, SCREEN_WIDTH), (float)SCREEN_HEIGHT - 1.0f);

        SpawnAsteroid(position, (float)GetRandomValue(39, 49));
    }
}

static int CountAsteroids(void)
{
    int count = 0;
    for (int i = 0; i < MAX_ASTEROIDS; i++) {
        if (asteroids[i].active) count++;
    }
    return count;
}

static void ResetGame(void)
{
    memset(asteroids, 0, sizeof(asteroids));
    memset(bullets, 0, sizeof(bullets));
    memset(particles, 0, sizeof(particles));

    ship.position = Vec2(SCREEN_WIDTH / 2.0f, SCREEN_HEIGHT / 2.0f);
    ship.velocity = Vec2(0.0f, 0.0f);
    ship.rotation = -90.0f;
    ship.invulnerability = 2.0f;
    ship.thrusting = false;

    score = 0;
    lives = 3;
    wave = 1;
    shootCooldown = 0.0f;
    gameScreen = SCREEN_PLAYING;
    SpawnWave(wave);
}

static void FireBullet(void)
{
    for (int i = 0; i < MAX_BULLETS; i++) {
        if (!bullets[i].active) {
            Vector2 forward = DirectionFromAngle(ship.rotation);
            bullets[i].active = true;
            bullets[i].position = Add(ship.position, Scale(forward, 21.0f));
            bullets[i].velocity = Add(ship.velocity, Scale(forward, 530.0f));
            bullets[i].life = 1.15f;
            return;
        }
    }
}

static void UpdateParticles(float deltaTime)
{
    for (int i = 0; i < MAX_PARTICLES; i++) {
        Particle *particle = &particles[i];
        if (!particle->active) continue;

        particle->position = Add(particle->position, Scale(particle->velocity, deltaTime));
        particle->velocity = Scale(particle->velocity, 1.0f - 0.8f * deltaTime);
        particle->life -= deltaTime;
        if (particle->life <= 0.0f) particle->active = false;
    }
}

static void UpdateGame(float deltaTime)
{
    const float turnSpeed = 235.0f;
    const float thrustPower = 280.0f;
    const float maxSpeed = 380.0f;

    if (IsKeyDown(KEY_LEFT) || IsKeyDown(KEY_A)) ship.rotation -= turnSpeed * deltaTime;
    if (IsKeyDown(KEY_RIGHT) || IsKeyDown(KEY_D)) ship.rotation += turnSpeed * deltaTime;

    ship.thrusting = IsKeyDown(KEY_UP) || IsKeyDown(KEY_W);
    if (ship.thrusting) {
        Vector2 forward = DirectionFromAngle(ship.rotation);
        ship.velocity = Add(ship.velocity, Scale(forward, thrustPower * deltaTime));
        if (GetRandomValue(0, 2) != 0) SpawnThrustParticle();
    }

    // Gentle drag keeps the ship controllable while preserving inertia.
    ship.velocity = Scale(ship.velocity, 1.0f - 0.12f * deltaTime);
    float speedSquared = LengthSquared(ship.velocity);
    if (speedSquared > maxSpeed * maxSpeed) {
        ship.velocity = Scale(ship.velocity, maxSpeed / sqrtf(speedSquared));
    }

    ship.position = Add(ship.position, Scale(ship.velocity, deltaTime));
    WrapPosition(&ship.position);
    if (ship.invulnerability > 0.0f) ship.invulnerability -= deltaTime;

    if (shootCooldown > 0.0f) shootCooldown -= deltaTime;
    if (IsKeyDown(KEY_SPACE) && shootCooldown <= 0.0f) {
        FireBullet();
        shootCooldown = 0.17f;
    }

    for (int i = 0; i < MAX_ASTEROIDS; i++) {
        Asteroid *asteroid = &asteroids[i];
        if (!asteroid->active) continue;
        asteroid->position = Add(asteroid->position, Scale(asteroid->velocity, deltaTime));
        asteroid->rotation += asteroid->rotationSpeed * deltaTime;
        WrapPosition(&asteroid->position);
    }

    for (int i = 0; i < MAX_BULLETS; i++) {
        Bullet *bullet = &bullets[i];
        if (!bullet->active) continue;
        bullet->position = Add(bullet->position, Scale(bullet->velocity, deltaTime));
        bullet->life -= deltaTime;
        WrapPosition(&bullet->position);
        if (bullet->life <= 0.0f) bullet->active = false;
    }

    // Bullet-versus-asteroid collisions.
    for (int b = 0; b < MAX_BULLETS; b++) {
        if (!bullets[b].active) continue;

        for (int a = 0; a < MAX_ASTEROIDS; a++) {
            Asteroid *asteroid = &asteroids[a];
            if (!asteroid->active) continue;

            float collisionRadius = asteroid->radius + 3.5f;
            if (DistanceSquared(bullets[b].position, asteroid->position) <=
                collisionRadius * collisionRadius) {
                Vector2 hitPosition = asteroid->position;
                float hitRadius = asteroid->radius;
                asteroid->active = false;
                bullets[b].active = false;

                score += (hitRadius > 31.0f) ? 20 : ((hitRadius > 18.0f) ? 50 : 100);
                SpawnExplosion(hitPosition, (hitRadius > 25.0f) ? 18 : 11, COLOR_ORANGE);

                // Large and medium rocks split into two smaller rocks.
                if (hitRadius > 20.5f) {
                    float childRadius = hitRadius * 0.55f;
                    Vector2 offset = Vec2((float)GetRandomValue(-12, 12),
                                          (float)GetRandomValue(-12, 12));
                    SpawnAsteroid(Add(hitPosition, offset), childRadius);
                    SpawnAsteroid(Subtract(hitPosition, offset), childRadius);
                }
                break;
            }
        }
    }

    // Ship-versus-asteroid collision. Respawn with temporary invulnerability.
    if (ship.invulnerability <= 0.0f) {
        for (int i = 0; i < MAX_ASTEROIDS; i++) {
            Asteroid *asteroid = &asteroids[i];
            if (!asteroid->active) continue;

            float collisionRadius = asteroid->radius + 13.0f;
            if (DistanceSquared(ship.position, asteroid->position) <=
                collisionRadius * collisionRadius) {
                SpawnExplosion(ship.position, 28, COLOR_CYAN);
                lives--;

                if (lives <= 0) {
                    gameScreen = SCREEN_GAME_OVER;
                    if (score > bestScore) bestScore = score;
                } else {
                    ship.position = Vec2(SCREEN_WIDTH / 2.0f, SCREEN_HEIGHT / 2.0f);
                    ship.velocity = Vec2(0.0f, 0.0f);
                    ship.rotation = -90.0f;
                    ship.invulnerability = 2.0f;
                }
                break;
            }
        }
    }

    if (gameScreen == SCREEN_PLAYING && CountAsteroids() == 0) {
        wave++;
        SpawnWave(wave);
    }
}

static void DrawParticles(void)
{
    for (int i = 0; i < MAX_PARTICLES; i++) {
        const Particle *particle = &particles[i];
        if (!particle->active) continue;

        float fade = particle->life / particle->maxLife;
        float radius = particle->size * (0.45f + 0.55f * fade);
        DrawCircleV(particle->position, radius, WithAlpha(particle->color, fade));
    }
}

static void DrawAsteroid(const Asteroid *asteroid)
{
    Vector2 points[ASTEROID_VERTICES];
    for (int i = 0; i < ASTEROID_VERTICES; i++) {
        float angle = asteroid->rotation + (360.0f / ASTEROID_VERTICES) * (float)i;
        points[i] = Add(asteroid->position,
                        Scale(DirectionFromAngle(angle), asteroid->radius * asteroid->shape[i]));
    }

    for (int i = 0; i < ASTEROID_VERTICES; i++) {
        int next = (i + 1) % ASTEROID_VERTICES;
        DrawLineEx(points[i], points[next], 4.0f, WithAlpha(COLOR_BLUE, 0.13f));
        DrawLineEx(points[i], points[next], 1.6f, COLOR_ASTEROID);
    }

    // A few inner facets make the rocks feel less like plain polygons.
    for (int i = 0; i < ASTEROID_VERTICES; i += 3) {
        DrawLineEx(asteroid->position, points[i], 1.0f, WithAlpha(COLOR_ASTEROID, 0.22f));
    }
}

static void DrawShip(void)
{
    Vector2 forward = DirectionFromAngle(ship.rotation);
    Vector2 nose = Add(ship.position, Scale(forward, 20.0f));
    Vector2 rearLeft = Add(ship.position, Scale(DirectionFromAngle(ship.rotation + 145.0f), 15.5f));
    Vector2 rearRight = Add(ship.position, Scale(DirectionFromAngle(ship.rotation - 145.0f), 15.5f));

    if (ship.thrusting) {
        Vector2 flameTip = Add(ship.position, Scale(forward, -27.0f - (float)GetRandomValue(0, 8)));
        DrawLineEx(rearLeft, flameTip, 3.5f, WithAlpha(COLOR_ORANGE, 0.65f));
        DrawLineEx(rearRight, flameTip, 3.5f, WithAlpha(COLOR_YELLOW, 0.65f));
    }

    DrawLineEx(nose, rearLeft, 5.0f, WithAlpha(COLOR_CYAN, 0.16f));
    DrawLineEx(rearLeft, rearRight, 5.0f, WithAlpha(COLOR_CYAN, 0.16f));
    DrawLineEx(rearRight, nose, 5.0f, WithAlpha(COLOR_CYAN, 0.16f));
    DrawLineEx(nose, rearLeft, 1.9f, COLOR_CYAN);
    DrawLineEx(rearLeft, rearRight, 1.9f, COLOR_CYAN);
    DrawLineEx(rearRight, nose, 1.9f, COLOR_CYAN);
    DrawCircleV(Add(ship.position, Scale(forward, 3.0f)), 2.2f, COLOR_YELLOW);

    if (ship.invulnerability > 0.0f) {
        DrawCircleLinesV(ship.position, 26.0f, WithAlpha(COLOR_CYAN, 0.28f));
        DrawCircleLinesV(ship.position, 30.0f, WithAlpha(COLOR_BLUE, 0.15f));
    }
}

static void DrawBullet(const Bullet *bullet)
{
    Vector2 trailDirection = DirectionFromAngle(atan2f(bullet->velocity.y, bullet->velocity.x) / DEG_TO_RAD);
    DrawLineEx(Subtract(bullet->position, Scale(trailDirection, 8.0f)),
               bullet->position, 3.0f, WithAlpha(COLOR_ORANGE, 0.30f));
    DrawCircleV(bullet->position, 2.4f, COLOR_YELLOW);
}

static void DrawHUD(void)
{
    DrawRectangle(0, 0, SCREEN_WIDTH, 76, WithAlpha(COLOR_PANEL, 0.91f));
    DrawLineEx(Vec2(0, 75), Vec2(SCREEN_WIDTH, 75), 1.0f, WithAlpha(COLOR_CYAN, 0.36f));

    DrawText("SCORE", 26, 12, 12, COLOR_MUTED);
    DrawText(TextFormat("%06i", score), 24, 31, 27, COLOR_CYAN);

    const char *waveText = TextFormat("WAVE  %02i", wave);
    DrawText(waveText, SCREEN_WIDTH / 2 - MeasureText(waveText, 21) / 2, 29, 21, COLOR_YELLOW);

    DrawText("HULL", SCREEN_WIDTH - 170, 12, 12, COLOR_MUTED);
    DrawText(TextFormat("%i / 3", lives), SCREEN_WIDTH - 171, 31, 25, COLOR_ORANGE);

    DrawText("W /   THRUST     A D / ARROWS  TURN     SPACE  FIRE",
             24, SCREEN_HEIGHT - 27, 13, WithAlpha(COLOR_MUTED, 0.86f));
    DrawText("WRAP-AROUND SECTOR  //  NO SAFE DIRECTION", SCREEN_WIDTH - 355,
             SCREEN_HEIGHT - 27, 11, WithAlpha(COLOR_MUTED, 0.63f));
}

static void DrawTitleScreen(void)
{
    const char *title = "ASTEROIDS";
    DrawText(title, SCREEN_WIDTH / 2 - MeasureText(title, 64) / 2, 214, 64, COLOR_CYAN);
    DrawText("VECTOR COMBAT  /  RAYLIB EDITION", SCREEN_WIDTH / 2 -
             MeasureText("VECTOR COMBAT  /  RAYLIB EDITION", 18) / 2,
             288, 18, COLOR_MUTED);

    // Decorative ship and drifting rocks on the title screen.
    DrawLineEx(Vec2(SCREEN_WIDTH / 2.0f - 22, 385), Vec2(SCREEN_WIDTH / 2.0f, 347),
               2.0f, WithAlpha(COLOR_CYAN, 0.65f));
    DrawLineEx(Vec2(SCREEN_WIDTH / 2.0f, 347), Vec2(SCREEN_WIDTH / 2.0f + 22, 385),
               2.0f, WithAlpha(COLOR_CYAN, 0.65f));
    DrawLineEx(Vec2(SCREEN_WIDTH / 2.0f + 22, 385), Vec2(SCREEN_WIDTH / 2.0f - 22, 385),
               2.0f, WithAlpha(COLOR_CYAN, 0.65f));
    DrawCircleLinesV(Vec2(300, 360), 37, WithAlpha(COLOR_BLUE, 0.55f));
    DrawCircleLinesV(Vec2(895, 410), 51, WithAlpha(COLOR_ASTEROID, 0.55f));
    DrawCircleLinesV(Vec2(800, 245), 19, WithAlpha(COLOR_ASTEROID, 0.42f));

    if (((int)(GetTime() * 2.0) % 2) == 0) {
        const char *startText = "PRESS ENTER TO LAUNCH";
        DrawText(startText, SCREEN_WIDTH / 2 - MeasureText(startText, 20) / 2,
                 470, 20, COLOR_YELLOW);
    }

    DrawText("W / UP THRUST    A / D OR ARROWS TURN    SPACE FIRE",
         24, SCREEN_HEIGHT - 27, 13,
         WithAlpha(COLOR_MUTED, 0.86f));
    DrawText("Destroy rocks. Avoid collisions. Survive each wave.",
             SCREEN_WIDTH / 2 - MeasureText("Destroy rocks. Avoid collisions. Survive each wave.", 16) / 2,
             563, 16, WithAlpha(COLOR_MUTED, 0.72f));
}

static void DrawGameOverScreen(void)
{
    DrawRectangle(0, 0, SCREEN_WIDTH, SCREEN_HEIGHT, WithAlpha(COLOR_BACKGROUND, 0.70f));
    const char *title = "SHIP LOST";
    const char *scoreText = TextFormat("FINAL SCORE  %06i", score);
    const char *bestText = TextFormat("SESSION BEST  %06i", bestScore);
    const char *restartText = "PRESS ENTER TO TRY AGAIN";

    DrawText(title, SCREEN_WIDTH / 2 - MeasureText(title, 48) / 2, 260, 48, COLOR_ORANGE);
    DrawText(scoreText, SCREEN_WIDTH / 2 - MeasureText(scoreText, 23) / 2,
             333, 23, COLOR_CYAN);
    DrawText(bestText, SCREEN_WIDTH / 2 - MeasureText(bestText, 17) / 2,
             372, 17, COLOR_MUTED);
    if (((int)(GetTime() * 2.0) % 2) == 0) {
        DrawText(restartText, SCREEN_WIDTH / 2 - MeasureText(restartText, 19) / 2,
                 440, 19, COLOR_YELLOW);
    }
}

int main(void)
{
    SetRandomSeed((unsigned int)time(NULL));
    InitWindow(SCREEN_WIDTH, SCREEN_HEIGHT, "ASTEROIDS // raylib");
    SetTargetFPS(60);
    InitializeStars();

    while (!WindowShouldClose()) {
        float deltaTime = GetFrameTime();
        // Avoid a large physics jump after dragging/minimizing the window.
        if (deltaTime > 0.04f) deltaTime = 0.04f;

        if (gameScreen == SCREEN_TITLE && IsKeyPressed(KEY_ENTER)) {
            ResetGame();
        } else if (gameScreen == SCREEN_PLAYING) {
            UpdateGame(deltaTime);
        } else if (gameScreen == SCREEN_GAME_OVER && IsKeyPressed(KEY_ENTER)) {
            ResetGame();
        }

        // Keep destruction particles animating briefly on the game-over screen too.
        UpdateParticles(deltaTime);

        BeginDrawing();
        ClearBackground(COLOR_BACKGROUND);
        DrawStarfield();

        if (gameScreen == SCREEN_PLAYING) {
            for (int i = 0; i < MAX_ASTEROIDS; i++) {
                if (asteroids[i].active) DrawAsteroid(&asteroids[i]);
            }
            for (int i = 0; i < MAX_BULLETS; i++) {
                if (bullets[i].active) DrawBullet(&bullets[i]);
            }
            DrawParticles();

            bool showShip = (ship.invulnerability <= 0.0f) ||
                            (((int)(GetTime() * 12.0) % 2) == 0);
            if (showShip) DrawShip();
            DrawHUD();
        } else if (gameScreen == SCREEN_TITLE) {
            DrawParticles();
            DrawTitleScreen();
        } else {
            DrawParticles();
            DrawGameOverScreen();
        }

        EndDrawing();
    }

    CloseWindow();
    return 0;
}
