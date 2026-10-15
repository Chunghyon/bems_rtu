현재의 폴더(littlefs_data)에 있는 모든 파일들은, 
main/CMakeLists.txt의 내용 중,
littlefs_create_partition_image(storage ../littlefs_data FLASH_IN_PROJECT)
에 의해서 idf.py flash 명령에 의해 메인 펌웨어와 함께 esp32에 업로드 됨.

littlefs_create_partition_image는 ESP32 프로젝트 빌드 과정에서 PC에 있는 특정 폴더의 파일들을 LittleFS 파일 시스템 이미지로 만들고, 이를 ESP32의 특정 파티션에 자동으로 굽도록(Flash) 설정하는 명령입니다.

이 한 줄이 수행하는 작업은 다음과 같습니다.

이미지 생성:

../littlefs_data 폴더 안에 있는 모든 파일(예: HTML, 이미지, 텍스트 파일 등)을 읽어들입니다.
이 파일들을 LittleFS 파일 시스템 구조로 변환하여 하나의 바이너리 이미지 파일(.bin)을 생성합니다.
파티션 매핑:

생성된 이미지를 ESP32 파티션 테이블에서 **storage**라는 이름을 가진 파티션에 할당합니다.
(파티션 테이블 CSV 파일에 storage라는 이름의 데이터 파티션이 정의되어 있어야 합니다.)
자동 플래싱 (FLASH_IN_PROJECT):

idf.py flash 명령어를 실행할 때, 메인 펌웨어와 함께 이 파일 시스템 이미지도 자동으로 ESP32에 업로드되도록 설정합니다.
명령어 구조 상세
요약
개발자가 ../littlefs_data 폴더에 파일(예: index.html)을 넣어두고 빌드(flash)하면, 별도의 코드를 작성하지 않아도 ESP32가 부팅될 때 /littlefs/index.html 경로로 해당 파일을 읽을 수 있게 됩니다.

이 기능은 웹 서버의 정적 리소스(HTML/CSS/JS)나 설정 파일 등을 배포할 때 매우 유용합니다.
