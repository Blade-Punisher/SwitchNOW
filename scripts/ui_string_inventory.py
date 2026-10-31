"""C++ UI-string inventory; static source audit, never executes application code."""
import re,json
from pathlib import Path
TOKEN=re.compile(r'//[^\n]*|/\*[\s\S]*?\*/|"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\'|[A-Za-z_]\w*|[^\s]')
SINKS={
 'Complete':(1,), 'swkbdConfigSetOkButtonText':(1,), 'addTab':(0,), 'swkbdInlineAppearArgSetOkButtonText':(1,), 'Tr':(0,), 'TrFormat':(0,), 'TrMessage':(0,), 'setText':(0,), 'notify':(0,),
 'setTitle':(0,), 'setHintText':(0,), 'addButton':(0,), 'registerAction':(0,),
 'Dialog':(0,), 'Dropdown':(0,), 'MakeLabel':(0,), 'MakeParagraph':(0,),
 'MakeToolbarButton':(0,), 'MakeSection':(0,1), 'MakeOptionRow':(0,1),
 'BuildMembershipBadge':(0,1), 'MakeInfoBlock':(0,1,2), 'openForText':(1,2), 'MakeActionRow':(0,1,2), 'AddInfoLine':(1,2), 'ShowError':(0,1),
 'ShowDialog':(0,1), 'PromptText':(0,), 'PromptCredential':(0,),
 'post_progress':(1,2), 'SetStatus':(0,1), 'draw_detail':(2,3),
 'draw_metric':(1,), 'draw_shortcut':(2,), 'nvgText':(3,), 'nvgTextBox':(4,),
}
def tokens(source):
 return [m for m in TOKEN.finditer(source) if not m.group().startswith(('//','/*'))]
def calls(source):
 ts=tokens(source)
 for i,t in enumerate(ts[:-1]):
  if t.group() not in SINKS or ts[i+1].group()!='(':continue
  depth=0;braces=0;brackets=0;start=i+2;args=[]
  for j in range(i+2,len(ts)):
   v=ts[j].group()
   if v==')' and depth==0:
    args.append(ts[start:j]);break
   if v==',' and depth==braces==brackets==0:args.append(ts[start:j]);start=j+1;continue
   if v=='(':depth+=1
   elif v==')':depth-=1
   elif v=='{':braces+=1
   elif v=='}':braces-=1
   elif v=='[':brackets+=1
   elif v==']':brackets-=1
  else:continue
  for index in SINKS[t.group()]:
   if index<len(args):yield t.group(),index,args[index]
def strings(argument):
 out=[];adjacent=False
 for t in argument:
  if t.group().startswith('"'):
   try:value=json.loads(t.group())
   except ValueError:continue
   if adjacent:out[-1]+=value
   else:out.append(value)
   adjacent=True
  else:adjacent=False
 return out
def inventory(root):
 result={}
 for p in sorted((root/'app/src').rglob('*')):
  if p.suffix not in ('.cpp','.hpp','.inc') or p.name.startswith('localization'):continue
  source=p.read_text(encoding='utf-8-sig')
  for match in re.finditer(r'(?:k\w*(?:Labels|SortModes|StoreFilters)|category_names)\s*(?:\[.*?\])?\s*[={]\s*\{?([^;]+);',source):
   for value in strings(tokens(match.group(1))):
    if re.search('[A-Za-z]{2}',value):result.setdefault(value,[]).append({'path':p.relative_to(root).as_posix(),'call':'display_table','line':source[:match.start()].count('\n')+1})
  for name,index,arg in calls(source):
   for value in strings(arg):
    if re.search('[A-Za-z]{2}',value):result.setdefault(value,[]).append({'path':p.relative_to(root).as_posix(),'call':name,'line':source[:arg[0].start()].count('\n')+1})
 return result
if __name__=='__main__':
 root=Path(__file__).resolve().parent.parent
 data=inventory(root)
 (root/'build/r73-precise-ui.json').write_text(json.dumps(data,ensure_ascii=False,indent=2),encoding='utf-8')
 print(len(data))
