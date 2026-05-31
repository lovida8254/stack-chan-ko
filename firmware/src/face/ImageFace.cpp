#include "ImageFace.h"

using namespace m5avatar;

static const char* PATHS[] = {
  "/face/neutral.jpg",
  "/face/happy.jpg",
  "/face/sad.jpg",
  "/face/angry.jpg",
  "/face/sleepy.jpg",
  "/face/doubt.jpg",
};

int ImageFace::exprIndex(Expression e) {
  switch (e) {
    case Expression::Neutral: return 0;
    case Expression::Happy:   return 1;
    case Expression::Sad:     return 2;
    case Expression::Angry:   return 3;
    case Expression::Sleepy:  return 4;
    case Expression::Doubt:   return 5;
  }
  return 0;
}

const char* ImageFace::exprPath(int i) {
  if (i < 0 || i >= NUM_EXPR) return PATHS[0];
  return PATHS[i];
}

bool ImageFace::sdHasAnyImage() {
  for (int i = 0; i < NUM_EXPR; i++) {
    if (SD.exists(PATHS[i])) return true;
  }
  return false;
}

ImageFace::ImageFace() : Face(), _anyLoaded(false) {
  for (int i = 0; i < NUM_EXPR; i++) _sprites[i] = nullptr;
  loadAll();
}

ImageFace::~ImageFace() {
  for (int i = 0; i < NUM_EXPR; i++) {
    if (_sprites[i]) {
      _sprites[i]->deleteSprite();
      delete _sprites[i];
      _sprites[i] = nullptr;
    }
  }
}

void ImageFace::loadAll() {
  const int W = M5.Display.width();
  const int H = M5.Display.height();
  for (int i = 0; i < NUM_EXPR; i++) {
    if (!SD.exists(PATHS[i])) {
      Serial.printf("[face] missing: %s\n", PATHS[i]);
      continue;
    }
    M5Canvas* spr = new M5Canvas(&M5.Display);
    spr->setPsram(true);
    spr->setColorDepth(16);
    if (!spr->createSprite(W, H)) {
      Serial.printf("[face] createSprite(%dx%d) failed for %s\n", W, H, PATHS[i]);
      delete spr;
      continue;
    }
    spr->fillSprite(TFT_BLACK);
    // M5GFX's drawJpgFile(SD, ...) template instantiation is broken for
    // fs::SDFS in this lib version (DataWrapperT<SDFS> is abstract). Workaround:
    // slurp the file into a PSRAM buffer and call drawJpg(buf, size) which
    // takes raw bytes and bypasses the broken template.
    File f = SD.open(PATHS[i], "r");
    if (!f) { Serial.printf("[face] open failed: %s\n", PATHS[i]); spr->deleteSprite(); delete spr; continue; }
    size_t sz = f.size();
    if (sz == 0 || sz > 2 * 1024 * 1024) { Serial.printf("[face] bad size %u: %s\n", (unsigned)sz, PATHS[i]); f.close(); spr->deleteSprite(); delete spr; continue; }
    uint8_t* buf = (uint8_t*)heap_caps_malloc(sz, MALLOC_CAP_SPIRAM);
    if (!buf) { Serial.printf("[face] PSRAM alloc failed (%u): %s\n", (unsigned)sz, PATHS[i]); f.close(); spr->deleteSprite(); delete spr; continue; }
    size_t got = f.read(buf, sz);
    f.close();
    if (got != sz) { Serial.printf("[face] short read %u/%u: %s\n", (unsigned)got, (unsigned)sz, PATHS[i]); free(buf); spr->deleteSprite(); delete spr; continue; }

    bool ok = spr->drawJpg(buf, sz);
    if (!ok) ok = spr->drawPng(buf, sz);
    free(buf);
    if (!ok) {
      Serial.printf("[face] decode failed: %s\n", PATHS[i]);
      spr->deleteSprite();
      delete spr;
      continue;
    }
    _sprites[i] = spr;
    _anyLoaded = true;
    Serial.printf("[face] loaded: %s\n", PATHS[i]);
  }
  Serial.printf("[face] ImageFace ready=%d\n", (int)_anyLoaded);
}

void ImageFace::draw(DrawContext* ctx) {
  if (!_anyLoaded) {
    // Nothing usable; fall back to default vector face.
    Face::draw(ctx);
    return;
  }
  int idx = exprIndex(ctx->getExpression());
  M5Canvas* src = _sprites[idx];
  if (src == nullptr) {
    // Specific emotion missing — try Neutral, else vector fallback.
    src = _sprites[exprIndex(Expression::Neutral)];
    if (src == nullptr) {
      Face::draw(ctx);
      return;
    }
  }
  // Push our cached emotion sprite straight to the LCD. Face::draw normally
  // ends with `sprite->pushSprite(&M5.Display, ...)`; we replace that with our
  // image directly. (No scale/rotation handling in v1 — face appears at 1:1.)
  src->pushSprite(&M5.Display, 0, 0);
}
