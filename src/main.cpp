#include <AnimatedGIF.h>
#include <Arduino.h>
#include <HTTPClient.h>
#include <M5Core2.h>
#include <WiFi.h>
#include <WiFiMulti.h>

#include "passwd.h"  // 自分用。これは削除してOK

// WIFI接続に必要な情報を記入してください
const char *ssid = SSID;          // Wi-Fi接続のアクセスポイント名
const char *password = PASSWORD;  // Wi-Fi接続のパスワード

// ================================================================

// libdeps\m5XXXXXXXXX\Time\Time.hにあるpaulstoffregen/TimeのTime.hを
// つぶさないとコンパイルに失敗する
// Time.hをリネームしてSDKのTime.hが読み込まれるように対処した
#include <Time.h>
#include <TimeLib.h>

#define LCD_WIDTH (320)      // LCDの横ドット数(M5Stack)
#define LCD_HEIGHT (240)     // LCDの縦ドット数(M5Stack)
#define DISPLAY_COUNT (120)  // LCD点灯時間(だいたい秒)
#define CHECK_INTERVAL (3)   // チェック間隔(だいたい秒)

unsigned long file_buffer_size;    // ダウンロードしたサイズ
unsigned char file_buffer[30000];  // ダウンロード用のバッファ

const char *ntpServer = "ntp.jst.mfeed.ad.jp";  // NTPサーバ名
const long gmtOffset_sec = 9 * 60 * 60;         // GMTとJSTの時差
const int daylightOffset_sec = 0;               // 夏時間

char gif_dir_path[30];    // GIFが格納されているディレクトリ名
char gif_file_name[100];  // GIFファイル名
char gif_url[200];        // GIFが格納されているURL

AnimatedGIF gif;  // GIF展開用

// 日本地図スプライト
TFT_eSprite mapimg = TFT_eSprite(&M5.Lcd);

bool displayOn;            // 点灯中フラグ
long displayOffCount = 0;  // 消灯までのカウント
long procCount = 0;        // 処理カウント

void vibrate(unsigned int vibLength = 500) {
  M5.Axp.SetLDOEnable(3, true);
  delay(vibLength);
  M5.Axp.SetLDOEnable(3, false);
}

long doHttpGet(String url, uint8_t *p_buffer, unsigned long *p_len) {
  HTTPClient http;

  http.begin(url);

  int httpCode = http.GET();
  unsigned long index = 0;

  if (httpCode <= 0) {
    http.end();
    return -1;
  }
  if (httpCode != HTTP_CODE_OK) {
    http.end();
    return -1;
  }

  WiFiClient *stream = http.getStreamPtr();

  int len = http.getSize();
  if (len != -1 && len > *p_len) {
    http.end();
    return -1;
  }

  while (http.connected() && (len > 0 || len == -1)) {
    size_t size = stream->available();

    if (size > 0) {
      if ((index + size) > *p_len) {
        http.end();
        return -1;
      }
      int c = stream->readBytes(&p_buffer[index], size);

      index += c;
      if (len > 0) {
        len -= c;
      }
    }
    delay(1);
  }

  http.end();
  *p_len = index;

  return 0;
}

// 地図スプライトに描画
void GIFDrawSprite(GIFDRAW *pDraw) {
  uint8_t *s;
  uint16_t *d, *usPalette, usTemp[1000];
  int x, y;

  usPalette = pDraw->pPalette;
  y = pDraw->iY + pDraw->y;  // current line

  s = pDraw->pPixels;
  if (pDraw->ucDisposalMethod == 2)  // restore to background color
  {
    for (x = 0; x < LCD_WIDTH; x++) {
      if (s[x] == pDraw->ucTransparent) s[x] = pDraw->ucBackground;
    }
    pDraw->ucHasTransparency = 0;
  }
  // Apply the new pixels to the main image
  if (pDraw->ucHasTransparency)  // if transparency used
  {
    uint8_t *pEnd, c, ucTransparent = pDraw->ucTransparent;
    int x, iCount;
    pEnd = s + pDraw->iWidth;
    x = 0;
    iCount = 0;  // count non-transparent pixels
    while (x < pDraw->iWidth) {
      c = ucTransparent - 1;
      d = usTemp;
      while (c != ucTransparent && s < pEnd) {
        c = *s++;
        if (c == ucTransparent)  // done, stop
        {
          s--;  // back up to treat it like transparent
        } else  // opaque
        {
          *d++ = usPalette[c];
          iCount++;
        }
      }            // while looking for opaque pixels
      if (iCount)  // any opaque pixels?
      {
        for (int xOffset = 0; xOffset < iCount; xOffset++) {
          int32_t dx =
              (int32_t)(((double)(x + xOffset) / pDraw->iWidth) * LCD_WIDTH);
          int32_t dy = (int32_t)(((double)y / pDraw->iHeight) * LCD_HEIGHT);
          mapimg.drawPixel(dx, dy, usTemp[xOffset]);
        }
        x += iCount;
        iCount = 0;
      }
      // no, look for a run of transparent pixels
      c = ucTransparent;
      while (c == ucTransparent && s < pEnd) {
        c = *s++;
        if (c == ucTransparent)
          iCount++;
        else
          s--;
      }
      if (iCount) {
        x += iCount;  // skip these
        iCount = 0;
      }
    }
  } else {
    s = pDraw->pPixels;
    // Translate the 8-bit pixels through the RGB565 palette (already byte
    // reversed)
    for (x = 0; x < pDraw->iWidth; x++) {
      int32_t dx = (int32_t)(((double)x / pDraw->iWidth) * LCD_WIDTH);
      int32_t dy = (int32_t)(((double)y / pDraw->iHeight) * LCD_HEIGHT);
      mapimg.drawPixel(dx, dy, usPalette[*s++]);
    }
  }
}

// LCDに直接描画
void GIFDrawLcd(GIFDRAW *pDraw) {
  uint8_t *s;
  uint16_t *d, *usPalette, usTemp[1000];
  int x, y;

  usPalette = pDraw->pPalette;
  y = pDraw->iY + pDraw->y;  // current line

  s = pDraw->pPixels;
  if (pDraw->ucDisposalMethod == 2)  // restore to background color
  {
    for (x = 0; x < LCD_WIDTH; x++) {
      if (s[x] == pDraw->ucTransparent) s[x] = pDraw->ucBackground;
    }
    pDraw->ucHasTransparency = 0;
  }
  // Apply the new pixels to the main image
  if (pDraw->ucHasTransparency)  // if transparency used
  {
    uint8_t *pEnd, c, ucTransparent = pDraw->ucTransparent;
    int x, iCount;
    pEnd = s + pDraw->iWidth;
    x = 0;
    iCount = 0;  // count non-transparent pixels
    while (x < pDraw->iWidth) {
      c = ucTransparent - 1;
      d = usTemp;
      while (c != ucTransparent && s < pEnd) {
        c = *s++;
        if (c == ucTransparent)  // done, stop
        {
          s--;  // back up to treat it like transparent
        } else  // opaque
        {
          *d++ = usPalette[c];
          iCount++;
        }
      }            // while looking for opaque pixels
      if (iCount)  // any opaque pixels?
      {
        for (int xOffset = 0; xOffset < iCount; xOffset++) {
          int32_t dx =
              (int32_t)(((double)(x + xOffset) / pDraw->iWidth) * LCD_WIDTH);
          int32_t dy = (int32_t)(((double)y / pDraw->iHeight) * LCD_HEIGHT);

          M5.Lcd.drawPixel(dx, dy, usTemp[xOffset]);
        }
        x += iCount;
        iCount = 0;
      }
      // no, look for a run of transparent pixels
      c = ucTransparent;
      while (c == ucTransparent && s < pEnd) {
        c = *s++;
        if (c == ucTransparent)
          iCount++;
        else
          s--;
      }
      if (iCount) {
        x += iCount;  // skip these
        iCount = 0;
      }
    }
  } else {
    s = pDraw->pPixels;
    // Translate the 8-bit pixels through the RGB565 palette (already byte
    // reversed)
    for (x = 0; x < pDraw->iWidth; x++) {
      int32_t dx = (int32_t)(((double)x / pDraw->iWidth) * LCD_WIDTH);
      int32_t dy = (int32_t)(((double)y / pDraw->iHeight) * LCD_HEIGHT);

      M5.Lcd.drawPixel(dx, dy, usPalette[*s++]);
    }
  }
}

// 強震モニタのデータをチェック
void checkKmoni() {
  // 現在日時を取得し対象日時を決定
  // サーバからデータを取得できないことがあるのでリアルタイム日時の1秒前を対象日時とする
  time_t timer;
  time(&timer);
  timer -= 1;
  struct tm timeinfo;
  localtime_r(&timer, &timeinfo);

  // 対象日時から強震モニタ上で取得したいデータのディレクトリ名とファイル名を決定
  sprintf(gif_dir_path, "%04d%02d%02d", (1900 + timeinfo.tm_year),
          (1 + timeinfo.tm_mon), timeinfo.tm_mday);
  sprintf(gif_file_name, "%s%02d%02d%02d", gif_dir_path, timeinfo.tm_hour,
          timeinfo.tm_min, timeinfo.tm_sec);

  // LCD描画開始
  M5.Lcd.startWrite();

  // 日本地図を描画
  M5.Lcd.fillScreen(TFT_WHITE);
  mapimg.pushSprite(((TFT_HEIGHT - LCD_WIDTH) / 2),
                    ((TFT_WIDTH - LCD_HEIGHT) / 2), TFT_TRANSPARENT);

  // 予想震度を取得
  sprintf(gif_url,
          "http://www.kmoni.bosai.go.jp/data/map_img/EstShindoImg_noto/eew/%s/"
          "%s.eew.gif",
          gif_dir_path, gif_file_name);
  file_buffer_size = sizeof(file_buffer);
  if (doHttpGet(gif_url, file_buffer, &file_buffer_size) == 0) {
    // EEWをダウンロードできたときから指定秒数分処理が回るまでLCDを点灯
    displayOffCount = DISPLAY_COUNT;
    // LCDが消灯中の場合は点灯、BEEPを鳴らしていない場合はBEEPを鳴らす
    if (!displayOn) {
      M5.Lcd.wakeup();
      M5.Lcd.setBrightness(255);
      displayOn = true;
      // M5.Speaker.setVolume(5);
      // M5.Speaker.tone(1200, 1000);
      vibrate(2000);
    }
    // 予想震度を描画
    gif.open((uint8_t *)file_buffer, file_buffer_size, GIFDrawLcd);
    while (gif.playFrame(true, NULL)) {
    }
    gif.close();
  }

  // LCDが点灯している状態の場合、震源とリアルタイム震度情報を取得を試みて
  // 取得できた場合は描画する
  if (displayOn) {
    // 震源・P波・S波
    sprintf(gif_url,
            "http://www.kmoni.bosai.go.jp/data/map_img/PSWaveImg_noto/eew/%s/"
            "%s.eew.gif",
            gif_dir_path, gif_file_name);
    file_buffer_size = sizeof(file_buffer);
    if (doHttpGet(gif_url, file_buffer, &file_buffer_size) == 0) {
      // 震源・P波・S波を描画
      gif.open((uint8_t *)file_buffer, file_buffer_size, GIFDrawLcd);
      while (gif.playFrame(true, NULL)) {
      }
      gif.close();
    }

    // リアルタイム震度
    sprintf(
        gif_url,
        "http://www.kmoni.bosai.go.jp/data/map_img/RealTimeImg_noto/jma_s/%s/"
        "%s.jma_s.gif",
        gif_dir_path, gif_file_name);
    file_buffer_size = sizeof(file_buffer);
    if (doHttpGet(gif_url, file_buffer, &file_buffer_size) == 0) {
      // リアルタイム震度を描画
      gif.open((uint8_t *)file_buffer, file_buffer_size, GIFDrawLcd);
      while (gif.playFrame(true, NULL)) {
      }
      gif.close();
    }
  }
}

void setup() {
  // M5Stackを初期化
  M5.begin();

  // LCDを初期化し、Wi-Fi接続、NTP同期状況を接続する
  M5.Lcd.begin();
  M5.Lcd.setSwapBytes(true);
  M5.Lcd.setRotation(1);
  M5.Lcd.setCursor(0, 0);
  M5.Lcd.fillScreen(TFT_BLUE);
  M5.Lcd.setTextColor(TFT_WHITE);
  M5.Lcd.setTextSize(2);
  M5.Lcd.println("[KYOSHIN]");
  M5.Lcd.println("");
  M5.Lcd.setTextSize(2);

  // Wi-Fi接続
  M5.Lcd.printf("Wi-Fi: Connecting to %s\n", ssid);
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    M5.Lcd.print(".");
  }
  M5.Lcd.print("\n");

  // NTP同期
  M5.Lcd.printf("NTP: Sync NTP Server(%s)\n", ntpServer);
  configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);
  struct tm timeinfo;
  if (!getLocalTime(&timeinfo)) {
    M5.Lcd.println("NTP synchronization failed");
    return;
  }

  gif.begin(LITTLE_ENDIAN_PIXELS);

  // サーバから日本地図を取得しスプライトとして描画
  file_buffer_size = sizeof(file_buffer);
  if (doHttpGet("https://raw.githubusercontent.com/kalaxity/Kyoshin_Noto_by_M5Core2/refs/heads/main/image/base_map_w.gif",
                file_buffer, &file_buffer_size) != 0) {
    M5.Lcd.println("Map download failed");
    return;
  }

  mapimg.setColorDepth(4);
  mapimg.createSprite(LCD_WIDTH, LCD_HEIGHT);
  mapimg.setSwapBytes(false);
  mapimg.fillScreen(TFT_TRANSPARENT);

  gif.open((uint8_t *)file_buffer, file_buffer_size, GIFDrawSprite);
  while (gif.playFrame(true, NULL)) {
  }
  gif.close();

  // スピーカーをミュート
  // M5.Speaker.mute();

  // 画面を塗りつぶし
  M5.Lcd.fillScreen(TFT_WHITE);

  // 画面を点灯
  M5.Lcd.wakeup();
  M5.Lcd.setBrightness(100);
  displayOn = true;
  displayOffCount = DISPLAY_COUNT;

  // M5.Speaker.setVolume(1);
  // M5.Speaker.tone(440, 50);
  // delay(50);
  // M5.Speaker.mute();
  vibrate(500);
}

void loop() {
  M5.update();

  // ボタンの状態をチェック
  if (M5.BtnA.wasPressed()) {
    // ボタンA -> LCDを点灯
    M5.Lcd.wakeup();
    M5.Lcd.setBrightness(255);
    displayOn = true;
    displayOffCount = DISPLAY_COUNT;
    // M5.Speaker.tone(1200, 50);
    // delay(20);
    // M5.Speaker.mute();
    vibrate(500);
  } else if (M5.BtnB.wasPressed()) {
    // ボタンB -> ミュート
    // M5.Speaker.mute();
  } else if (M5.BtnC.wasPressed()) {
    // ボタンC -> LCDを消灯
    M5.Lcd.sleep();
    M5.Lcd.setBrightness(0);
    displayOn = false;
    displayOffCount = 0;
    // M5.Speaker.tone(1200, 50);
    // delay(20);
    // M5.Speaker.mute();
    vibrate(500);
  }

  // 強震モニタをチェックする
  if (procCount % CHECK_INTERVAL == 0) {
    checkKmoni();
  }

  // LCD消灯までカウントダウン
  if (--displayOffCount <= 0) {
    if (displayOn) {
      // LCDを消灯
      M5.Lcd.sleep();
      M5.Lcd.setBrightness(0);
      displayOn = false;
      // BEEPをミュート
      // M5.Speaker.mute();
    }
  }

  // 処理カウンタを進める
  procCount++;
  if (procCount >= CHECK_INTERVAL) {
    procCount = 0;
  }

  // 次の処理まで待機
  delay(1000);
}
