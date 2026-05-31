#pragma once

// CoreS3 では LCD と SD カードが同じ SPI バスを共有し、GPIO35 が LCD の D/C と
// SD の MISO を兼ねている(M5GFX の Panel_M5StackCoreS3::cs_control 参照)。
// アバターは別タスクで LCD を描画し続けるため、SD 読み込みの瞬間に GPIO35 が
// D/C 出力状態だと SD が MISO を駆動できず読み込みが失敗する
// ("... does not exist" / ff_sd_status)。
//
// sd_bus_lock() : アバターの描画タスクを停止し、GPIO35 を強制的に FSPI MISO 入力にする。
// sd_bus_unlock(): 描画タスクを再開する。
// SD への連続アクセス(写真ロード・MP3 ストリーミング等)をこの2つで挟む。
void sd_bus_lock();
void sd_bus_unlock();
