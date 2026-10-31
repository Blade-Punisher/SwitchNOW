import { Image, Rect, Text } from 'react-tela';
export interface GameContext {
  title?: string; stores?: string; description?: string; language?: string;
  preview_background?: string; preview_cover?: string; preview_screenshots?: string[];
}
export function InstallerView({ context, stage, failed }: {
  context: GameContext; stage: number; failed: boolean;
}) {
  const ru = context.language === 'ru';
  const steps = ru ? ['Подготовка ярлыка', 'Создание приложения', 'Установка в HOME', 'Возвращаемся к игре']
    : ['Preparing shortcut', 'Creating application', 'Installing on HOME', 'Returning to your game'];
  return <>
    <Rect fill='#0a0e13' width={1280} height={720} x={0} y={0} />
    {context.preview_background && <Image src={context.preview_background}
      x={0} y={0} width={1280} height={720} />}
    <Rect fill='rgba(5, 9, 14, 0.52)' width={1280} height={720} x={0} y={0} />
    <Rect fill='#1c2d35' x={315} y={231} width={650} height={254} />
    <Rect fill='#152129' x={316} y={232} width={648} height={252} />
    <Rect fill={failed ? '#ee8a82' : '#54dc91'} x={348} y={267} width={5} height={31} />
    <Text fill='#f2f6fa' fontSize={27} x={370} y={263}>
      {failed ? (ru ? 'Не удалось создать ярлык' : 'Shortcut could not be created')
        : (ru ? 'Ярлык на HOME' : 'HOME shortcut')}
    </Text>
    <Text fill='#c5d3dd' fontSize={22} x={350} y={322} maxWidth={565} overflow='ellipsis'>
      {failed ? (ru ? 'Возвращаемся к карточке игры…' : 'Returning to the game page…') : steps[Math.min(stage, 3)]}
    </Text>
    <Rect fill='#2b3e47' x={350} y={375} width={580} height={7} />
    <Rect fill={failed ? '#ee8a82' : '#54dc91'} x={350} y={375} width={145 * (Math.min(stage, 3) + 1)} height={7} />
    <Text fill='#8da5b3' fontSize={18} x={350} y={411} maxWidth={580}>
      {ru ? 'После установки вы вернётесь к игре.' : 'Your game page will return after installation.'}
    </Text>
  </>;
}
