# WT32 SmallTV

[WT32-SC01 Plus](docs/WT32-SC01-PLUS_Datasheet-V1.9+EN.pdf) (ESP32-S3 + 3.5인치
480x320 터치 LCD) 보드용 **탁상시계 / 디지털 액자** 펌웨어입니다. ESP-IDF와
LVGL 위에서 순수 C 언어로 작성되었으며, **GeekMagic SmallTV Ultra** 를
오마주했습니다: 시계 화면, microSD 카드 기반 사진 슬라이드쇼, 기기 정보 화면을
스와이프로 전환할 수 있고, 별도 앱이나 클라우드 계정 없이 기기 자체가 제공하는
웹 페이지로 모든 설정을 마칠 수 있습니다.

> English documentation: [README.md](README.md)

## 주요 기능

- **시계 화면** - 큰 디지털 시각 + 날짜, SNTP로 자동 동기화.
- **전자앨범** - microSD 카드의 `.bmp` / `.jpg` 이미지를 슬라이드쇼로 재생,
  스와이프/탭으로 다음 사진 전환, 재생 간격과 셔플 여부 설정 가능.
- **기기 정보 화면** - IP 주소, Wi-Fi 신호, SD 카드 상태, 가동 시간, 여유 힙,
  펌웨어 버전.
- 화면 간 **스와이프 전환** + 선택적 자동 순환.
- **완전 웹 기반 설정** - 기기가 직접 제공하는 작은 HTML5/CSS/JS 페이지:
  - Wi-Fi 설정(스캔 + 연결), 항상 켜져 있는 SoftAP + 캡티브 포털로 언제나
    설정에 접근 가능.
  - 시간대(POSIX TZ 문자열), NTP 서버, 12/24시간 표시, 정시 알림음.
  - 밝기, 자동 순환, 음소거.
  - 전자앨범 재생 간격/셔플.
  - 호스트명, 재시작, 공장 초기화.
  - **펌웨어 업데이트**: `.bin` 파일을 직접 업로드하거나, GitHub 저장소의
    릴리즈를 확인해 한 번의 클릭으로 설치 - [펌웨어 업데이트](#펌웨어-업데이트)
    참고.
- IP 주소 또는 mDNS(`http://wt32-smalltv.local/`, 기본값)로 접속 가능.
- 온보드 I2S 앰프를 통한 부팅/UI 알림음.

## 하드웨어

[WT32-SC01 Plus](https://www.wireless-tag.com) (`ZX3D50CE08S-USRC-4832`):
ESP32-S3 (WT32-S3-WROVER-N16R2, 16MB 플래시 + PSRAM), 8080/i80 병렬 버스로
연결된 480x320 ST7796 LCD, I2C 정전식 터치 FT6336U, SPI microSD, 2.5W/4R I2S
클래스-D 앰프. 전체 데이터시트는 [`docs/`](docs/) 폴더를 참고하세요.

### 핀맵 (제조사 데이터시트 기준)

| 기능              | GPIO                                        |
|-------------------|----------------------------------------------|
| LCD 데이터 D0-D7  | 9, 46, 3, 8, 18, 17, 16, 15                   |
| LCD DC/WR/RST     | 0 / 47 / 4 (RST는 터치와 공유)                |
| LCD 백라이트      | 45 (PWM, active high)                         |
| 터치 SDA/SCL/INT  | 6 / 5 / 7 (FT6336U, I2C)                      |
| SD CS/MOSI/CLK/MISO | 41 / 40 / 39 / 38 (SPI)                    |
| I2S BCLK/WS/DOUT  | 36 / 35 / 37                                  |
| 확장 IO           | 10, 11, 12, 13, 14, 21                        |
| RS485 RX/RTS/TX   | 1 / 2 / 42 (이 펌웨어에서는 사용하지 않음)     |

코드의 단일 기준(single source of truth)은
[`components/bsp/include/bsp/bsp_pins.h`](components/bsp/include/bsp/bsp_pins.h) 입니다.

## 빌드 방법

**ESP-IDF v6.0 이상**이 필요합니다 (v6.1 기준으로 작성 및 테스트되었습니다).
C 언어만 사용하며 C++ 코드는 전혀 없습니다.

ESP-IDF v6.0에서는 주변장치 드라이버 컴포넌트 구조가 크게 바뀌었고(각
주변장치가 예전의 단일 `driver` 컴포넌트 대신 개별 `esp_driver_xxx`
컴포넌트로 분리됨), 내장 `json` 컴포넌트도 제거되어 관리형 컴포넌트
`espressif/cjson` 로 대체되었습니다. 두 가지 모두 이 프로젝트의
`CMakeLists.txt`/`idf_component.yml`에 이미 반영되어 있으므로 그대로
빌드하면 됩니다. 만약 더 이전 버전(v5.2~v5.5)을 타깃으로 해야 한다면,
[`bsp_display.c`](components/bsp/bsp_display.c)의
`esp_lcd_i80_bus_config_t::dma_burst_size`(예전에는
`psram_trans_align`/`sram_trans_align`)와 `app_web`/`app_ota`의
`REQUIRES json` / cJSON 관리형 컴포넌트 분리 부분을 되돌려야 합니다.

```sh
. $IDF_PATH/export.sh
idf.py set-target esp32s3
idf.py build
idf.py -p <PORT> flash monitor
```

첫 빌드 시 컴포넌트 매니저가 관리형 의존성(`lvgl`, `esp_lvgl_port`,
`esp_lcd_st7796`, `esp_lcd_touch_ft5x06`, `esp_jpeg`)을 자동으로 내려받습니다.

VS Code Dev Containers로 공식 `espressif/idf` Docker 이미지 안에서 빌드하고
싶다면 `.devcontainer/`를 사용하세요.

## 최초 부팅 / 설정 방법

1. 펌웨어를 플래시하고 보드에 전원을 인가합니다.
2. 정보 화면(스와이프로 이동)에 `WT32-SmallTV-AB12` 같은 SoftAP 이름과 비밀번호
   `smalltv1234` 가 표시됩니다. 휴대폰이나 노트북을 이 AP에 연결하면 대부분
   자동으로 캡티브 포털 창이 뜨고, 뜨지 않으면 `http://192.168.4.1/` 을
   직접 엽니다.
3. **Wi-Fi** 탭에서 스캔 후 원하는 네트워크를 선택하고 비밀번호를 입력한 뒤
   *Connect*를 누릅니다. 성공하면 기기가 해당 네트워크에 연결되고, 설정은
   재부팅 후에도 유지됩니다.
4. SoftAP과 캡티브 포털은 항상 켜져 있으므로, 홈 네트워크가 끊기더라도
   AP를 통해 언제든 웹 UI에 접근할 수 있습니다. 연결 후에는
   `http://<기기IP>/` 또는 `http://wt32-smalltv.local/` 로도 접속 가능합니다.
5. 나머지 탭에서 시간대, NTP 서버 등을 원하는 대로 설정합니다.

### 전자앨범 사용법

microSD 카드 루트에 `photos/` 폴더를 만들고 `.bmp`(24비트 무압축) 및/또는
`.jpg`/`.jpeg` 파일을 넣으세요 (`/photos/*.bmp`). 이미지는 480x320 화면에
맞게 자동으로 스케일링되며, 최상의 화질을 원한다면 미리 480x320으로
편집해서 넣는 것을 권장합니다. BMP는 외부 의존성이 전혀 없어 항상 동작이
보장되는 포맷이며, JPEG는 `espressif/esp_jpeg` 관리형 컴포넌트를 통해
디코딩됩니다 (아래 "알려진 한계" 참고).

## 펌웨어 업데이트

웹 UI의 **Firmware** 탭에서 두 가지 방법으로 업데이트할 수 있으며, 둘 다
동일한 온디바이스 검증 절차를 거칩니다:

- **수동 업로드** - `idf.py build`로 만든 `.bin` 파일
  (`build/wt32_smalltv.bin`)을 선택하고 *Upload & install*을 누릅니다.
  업로드 진행률이 표시됩니다.
- **GitHub 릴리즈 확인** - "Check GitHub releases"에 `owner/name` 형식으로
  저장소를 입력해 저장한 뒤 *Check for updates*를 누릅니다. 기기가
  `https://api.github.com/repos/<owner>/<name>/releases/latest` 에서
  `firmware.bin` 이라는 이름의 에셋(없으면 첫 번째 `.bin` 에셋)을 찾아,
  버전 태그가 현재보다 최신이면 설치를 제안합니다. 포크를 유지 관리한다면
  빌드한 `build/wt32_smalltv.bin` 을 각 GitHub Release에 `firmware.bin`
  이라는 이름으로 올리고 `vX.Y.Z` 형식으로 태그하세요.

**엉뚱한/잘못된 `.bin` 파일이 기기를 망가뜨리지 않도록 하는 검증 단계:**
1. `esp_ota_write()`/`esp_ota_end()` (GitHub 경로는 `esp_https_ota_*`)가
   이미 올바른 형식의 체크섬이 있는 ESP32 이미지가 아니면 거부합니다 -
   임의의 파일이나 다운로드 도중 잘린 파일은 이 단계에서 걸러집니다.
2. 여기에 더해, 모든 이미지에 내장된 `esp_app_desc_t.project_name` 값을
   이 프로젝트의 식별자와 비교합니다 (
   [`components/app_ota/app_ota.c`](components/app_ota/app_ota.c) 의
   `FIRMWARE_PROJECT_NAME` 참고) - 이는 "유효한 ESP32 펌웨어이지만 이
   프로젝트의 것은 아닌" 경우까지 잡아냅니다.
3. 마지막 안전장치로 `CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE` 이 켜져
   있습니다: 새로 설치된 이미지는 `app_main()` 이 UI를 정상적으로 띄운
   뒤에야 "확인됨"으로 표시됩니다. 그 전에 크래시하거나 재부팅되면,
   부트로더가 다음 부팅 시 자동으로 이전의 정상 동작하던 이미지로
   되돌립니다.

파티션 테이블(`partitions.csv`)은 표준적인 `otadata` + `ota_0`/`ota_1`
이중 앱 슬롯 구조를 사용합니다(`factory` 파티션 없음). 따라서 최초 1회
시리얼로 `idf.py -p <PORT> flash` 하는 것도 그대로 동작합니다.

## 프로젝트 구조

```
main/                 app_main(): 초기화 순서와 배선만 담당
components/
  bsp/                 보드 지원: 디스플레이+터치(esp_lvgl_port 경유),
                       SD 카드, 백라이트 PWM, I2S 오디오
  app_config/          NVS 기반 설정 구조체 (단일 기준)
  app_wifi/            SoftAP + STA Wi-Fi 매니저, 캡티브 포털 DNS, mDNS
  app_time/            시간대 + SNTP
  app_photo/           SD 카드 스캔 + BMP/JPEG 디코딩 + 리사이즈
  app_web/             REST API + 내장 HTML5/CSS/JS 설정 페이지
  app_ota/             수동/GitHub 펌웨어 업데이트 + 신원 검증
  ui/                  LVGL 화면 (시계/앨범/정보) + 화면 전환
```

## 알려진 한계 / 로드맵

- **RTC 배터리 없음** - 보드에 배터리 백업 RTC 칩이 없어, 부팅 후 SNTP 동기화가
  최초 1회 성공해야만 시각이 정확해집니다.
- **아날로그 시계 화면** - 향후 확장을 위해 설정 구조체에 `clock_face` 값은
  이미 있지만, 현재는 디지털 화면만 구현되어 있습니다.
- **JPEG 디코딩은 best-effort** - `esp_jpeg` 컴포넌트의 API는 버전별로 조금씩
  바뀌어 왔습니다. 컴포넌트 업그레이드로 JPEG 디코딩이 깨지더라도 BMP 사진은
  영향받지 않고 계속 동작합니다 (`components/app_photo/app_photo_jpeg.c` 참고).
- **OTA 에셋 이름 규칙** - GitHub 업데이트 확인은 우선 `firmware.bin` 이라는
  이름의 에셋만 찾고, 없으면 첫 번째 `.bin` 파일을 사용합니다. 릴리즈에
  여러 보드용 바이너리가 함께 있다면 `firmware.bin` 이 이 보드용 파일이
  되도록 이름을 맞춰주세요 (또는 저장소별로 에셋명을 조정하세요).
- 추가 아이디어 환영: 날씨 화면, SD 카드 MP3/오디오 재생, 알람, 추가 시계
  화면, GIF 재생.

## 보안 참고사항

폴백 SoftAP은 항상 설정 화면에 접근할 수 있도록 고정 기본 비밀번호
(`smalltv1234`)를 사용합니다. 이는 다른 SmallTV류 기기나 저가 공유기와
마찬가지로 "같은 공간에 있는 사람은 신뢰한다"는 모델이며, Wi-Fi 전파가
닿는 범위의 악의적인 공격자를 막기 위한 용도가 아님을 참고하세요.

## 라이선스

MIT - [LICENSE](LICENSE) 참고. `docs/` 폴더의 제조사 데이터시트는 참고용으로
포함된 것이며, 저작권은 Wireless-Tag Technology Co., Limited 에 있습니다.
