@echo off
rem 깃허브에서 최신 코드를 받아온다 (main 브랜치). 내 작업과 겹치면 멈추고 알려준다
chcp 65001 > nul
cd /d "%~dp0"
echo [pull] %cd%
git pull --ff-only
if errorlevel 1 (
    echo.
    echo [pull] 실패: 커밋 안 한 변경이 겹치거나 내 커밋과 갈라졌습니다. git status 로 확인하세요.
) else (
    echo.
    echo [pull] 완료. 비주얼 스튜디오에서 다시 빌드하세요.
)
pause
