#ifndef _IMAGE_FACE_H
#define _IMAGE_FACE_H

#include <M5Unified.h>
#include <SD.h>
#include "Face.h"
#include "Expression.h"
#include "DrawContext.h"

// Custom Face that replaces m5stack-avatar's vector-drawn eyes/mouth/eyebrows
// with full-screen JPEG images loaded from the SD card, one per emotion.
// Files expected on SD (any subset; missing emotions fall back to Neutral or
// to the default Face::draw):
//
//   /face/neutral.jpg
//   /face/happy.jpg
//   /face/sad.jpg
//   /face/angry.jpg
//   /face/sleepy.jpg
//   /face/doubt.jpg
//
// Each image is decoded once at boot into an in-memory M5Canvas sprite (PSRAM)
// sized to the LCD (320 x 240 on CoreS3) so per-frame draw is just a sprite
// blit — no per-frame SD I/O or JPEG decode. Trade-off vs. the vector face:
// no lipsync mouth animation in v1 (the image covers the whole face).
class ImageFace : public m5avatar::Face {
public:
  ImageFace();
  virtual ~ImageFace();

  virtual void draw(m5avatar::DrawContext* ctx) override;

  bool isReady() const { return _anyLoaded; }

  // Static probe used by main.cpp setup to decide whether to swap the face.
  // Returns true if at least one /face/*.jpg exists on SD.
  static bool sdHasAnyImage();

private:
  static const int NUM_EXPR = 6;
  M5Canvas* _sprites[NUM_EXPR];
  bool _anyLoaded;

  void loadAll();
  static int exprIndex(m5avatar::Expression e);
  static const char* exprPath(int i);
};

#endif  // _IMAGE_FACE_H
