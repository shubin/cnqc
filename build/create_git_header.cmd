@echo off
FOR /F "tokens=* USEBACKQ" %%F IN (`git rev-parse HEAD`) DO (
SET GIT_COMMIT_HASH=%%F
)
FOR /F "tokens=* USEBACKQ" %%F IN (`git rev-parse --abbrev-ref HEAD`) DO (
SET GIT_BRANCH_NAME=%%F
)
:: We create the file in one go to avoid doubled lines in multi-threaded builds.
(echo #define GIT_COMMIT ^"%GIT_COMMIT_HASH%^" && echo #define GIT_BRANCH ^"%GIT_BRANCH_NAME%^")>%1
