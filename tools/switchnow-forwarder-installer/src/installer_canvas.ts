export interface InstallerState { language?: string; stage: number; failed: boolean; }
// Draw the panel independently of artwork decoding or React reconciliation.
export function paintInstaller(ctx: CanvasRenderingContext2D, width: number, height: number,
  state: InstallerState, background?: ImageBitmap): void {
  const ru = state.language === 'ru';
  const steps = ru ? ['Подготовка ярлыка', 'Создание приложения', 'Установка в HOME', 'Возвращаемся к игре']
    : ['Preparing shortcut', 'Creating application', 'Installing on HOME', 'Returning to your game'];
  ctx.save();
  try {
    ctx.setTransform(width / 1280, 0, 0, height / 720, 0, 0);
    ctx.fillStyle = '#0a0e13'; ctx.fillRect(0, 0, 1280, 720);
    if (background) {
      try { ctx.drawImage(background, 0, 0, 1280, 720); } catch { /* Panel remains visible. */ }
    }
    ctx.fillStyle = 'rgba(5,9,14,0.52)'; ctx.fillRect(0, 0, 1280, 720);
    ctx.fillStyle = '#152129'; ctx.fillRect(315, 231, 650, 254);
    ctx.fillStyle = state.failed ? '#ee8a82' : '#54dc91'; ctx.fillRect(348, 267, 5, 31);
    ctx.font = '27px system-ui'; ctx.fillStyle = '#f2f6fa';
    ctx.fillText(state.failed ? (ru ? 'Не удалось создать ярлык' : 'Shortcut could not be created')
      : (ru ? 'Ярлык на HOME' : 'HOME shortcut'), 370, 287, 565);
    ctx.font = '22px system-ui'; ctx.fillStyle = '#c5d3dd';
    ctx.fillText(state.failed ? (ru ? 'Возвращаемся к игре…' : 'Returning to game…')
      : steps[Math.max(0, Math.min(state.stage, 3))], 350, 345, 565);
    ctx.fillStyle = '#2b3e47'; ctx.fillRect(350, 375, 580, 7);
    ctx.fillStyle = state.failed ? '#ee8a82' : '#54dc91';
    ctx.fillRect(350, 375, 145 * (Math.max(0, Math.min(state.stage, 3)) + 1), 7);
    ctx.font = '18px system-ui'; ctx.fillStyle = '#8da5b3';
    ctx.fillText(ru ? 'После установки вы вернётесь к игре.' : 'Your game page will return after installation.', 350, 433, 580);
    if (state.failed) ctx.fillText(ru ? 'Если возврат не сработал: нажмите +.' : 'If return fails: press +.', 350, 463, 580);
  } finally { ctx.restore(); }
}
