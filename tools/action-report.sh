#!/system/bin/sh
# Sourced by action.sh for the KernelSU Action button (no arguments).
echo 'Действие: проверка состояния модуля.'
echo 'Это нажатие не является командой выключения или перезапуска.'
echo
if [ -f "$MODDIR/disable" ]; then
  echo 'Разрешение KernelSU: выключено. Запуск запрещён.'
else
  echo 'Разрешение KernelSU: включено. Ниже — фактическое состояние патча.'
fi
echo
if sh "$MODDIR/action.sh" status; then
  REPORT_CODE=0
else
  REPORT_CODE=$?
  echo "Не удалось подтвердить состояние (код $REPORT_CODE). Причина указана выше."
fi
echo
echo 'Как читать результат:'
echo 'MAIN / CORRECTED / EXTRAPOLATION — патч применён.'
echo 'STOCK — исходное состояние, патч не применён.'
echo 'WAITING — целевой процесс ещё не запущен или OpenXR не загружен.'
echo 'В режиме WAITING откройте нужное приложение и повторите проверку.'
echo
echo 'Повторный запуск:'
echo 'Сначала убедитесь, что галочка модуля включена.'
echo 'В текущем KernelSU переключение галочки само по себе не запускает патч немедленно.'
echo 'Для явного повторного запуска выполните в root-терминале:'
printf 'sh %s/action.sh on\n' "$MODDIR"
echo 'Команда on повторит запуск; в автоматическом варианте также запустит наблюдение.'
echo 'Не повторяйте запуск при несовместимой прошивке или конфликте модулей — сначала устраните причину.'
echo 'Для остановки с откатом:'
printf 'sh %s/action.sh off\n' "$MODDIR"
echo 'После запуска снова нажмите «Действие», чтобы проверить результат.'
exit "$REPORT_CODE"
