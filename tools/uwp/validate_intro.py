#!/usr/bin/env python3
"""Drive only the isolated console core, never the user's visible application.

Creates a fresh user/settings folder; does not load or change player saves.
Validates boot, new game, name completion and story dialogue through the
first duel using the native frame/control channel, with captured pictures.
"""
import argparse,json,sys,tempfile
from pathlib import Path
ROOT=Path(__file__).resolve().parents[2]
sys.path.insert(0,str(ROOT/'tools/pc'))
from yfm_control import Game,MODE

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--executable',type=Path,default=ROOT/'tmp/uwp-native/memories-uwp-headless.exe')
    parser.add_argument('--disc',type=Path,required=True)
    parser.add_argument('--out',type=Path)
    parser.add_argument('--mods',action='store_true')
    parser.add_argument('--assets',type=Path,help='program folder containing mods/languages')
    parser.add_argument('--host-callbacks',action='store_true',help='exercise video/audio callbacks with flags set to 0; no window or audio device')
    args=parser.parse_args()
    output=args.out or Path(tempfile.mkdtemp(prefix='native-intro-',dir=ROOT.parent/'artifacts'))
    executable=args.executable.resolve()
    assets=(args.assets or executable.parent).resolve()
    settings={}
    if not args.mods:
        for manifest in (assets/'mods').glob('*/mod.json'):
            settings['mod.'+json.loads(manifest.read_text(encoding='utf-8'))['id']]=0
    record={'executable':str(executable),'disc':str(args.disc.resolve()),'mods':args.mods,'pictures':{}}
    environment={'MEMORIES_DISC':str(args.disc.resolve()),'MEMORIES_PROGRAM_DIR':str(assets),'MEMORIES_CLOCK':'cooperative'}
    if args.host_callbacks:
        environment.update(MEMORIES_HEADLESS='0',MEMORIES_NO_AUDIO='0',MEMORIES_DUMP_AUDIO=str(output/'audio.s16le.pcm'))
    print('Isolated validation folder:',output,flush=True)
    with Game(executable,out=output,settings=settings,env=environment,mods_dir=assets/'mods') as game:
        def shot(name):
            path=game.shot(name+'.png')
            record['pictures'][name]={'frame':game.frame,'vram':f'{game.hash():016x}','path':str(path)}
            print(name,'frame',game.frame,flush=True)
        game.wait_until(lambda g:g.resident('main_menu'),3000,what='title')
        game.press_until(lambda g:g.resident('password'),['start','cross'],every=40,what='name entry')
        game.press_until(lambda g:g.player_name(),'cross',every=20,what='first name letter')
        shot('01-name')
        game.press('start')
        game.press_until(lambda g:g.mode()==MODE['campaign'],'cross',every=40,what='story')
        game.wait_until(lambda g:0x500<=g.u16(0x800EB12E)<0x1000 and bool(g.u16(0x800EB12C)&0x4000),
                        4000,every=5,what='first active story text')
        record['first_dialogue_id']=f'{game.u16(0x800EB12E):04x}'
        game.step(150)
        shot('02-first-dialogue')
        game.press_until(lambda g:g.mode()==MODE['duel'],'cross',every=40,timeout=20000,what='story dialogue to first duel')
        shot('03-dialogues-complete')
        record['first_duel_frame']=game.frame
        game.press_until(lambda g:not g.resident('main_menu'),'circle',every=60,what='duel field')
        game.wait_until(lambda g:all(g.duel()[0]['hand']),3000,every=10,what='first hand')
        game.step(200)
        shot('04-first-hand')
        record['player_name_guest_bytes']=game.player_name().hex()
        record['state']=game.state()
        record['passed']=True
    (output/'result.json').write_text(json.dumps(record,indent=2)+'\n',encoding='utf-8')
    print('PASS: intro, name, dialogues, first duel; result:',output/'result.json',flush=True)

if __name__=='__main__':main()
