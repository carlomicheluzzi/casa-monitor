#!/usr/bin/env python3
"""USB bootstrap with backup and partition check. Without --flash it only prints the plan."""
import argparse,hashlib,json,os,subprocess,sys,time,shlex
from pathlib import Path
ROOT=Path(__file__).resolve().parents[2]
TOOLS=ROOT/'.local/firmware-tools/data/packages/esp32'
OUT=ROOT/'.local/firmware-output'
EXPECTED='44:b1:76:b7:8f:70'

def flash_settings(arguments,bootloader):
    lines=arguments.read_text().splitlines()
    args=shlex.split(lines[0]) if lines else []
    allowed={'--flash-mode','--flash-freq','--flash-size'}
    if len(args)!=6 or set(args[::2])!=allowed:raise RuntimeError('Parametri flash della build non validi: scrittura annullata.')
    values=dict(zip(args[::2],args[1::2]))
    with bootloader.open('rb') as f:header=f.read(4)
    modes={0:'qio',1:'qout',2:'dio',3:'dout'};frequencies={0:'40m',1:'26m',2:'20m',15:'80m'}
    if len(header)!=4 or header[0]!=0xe9 or values['--flash-mode']!=modes.get(header[2]) or values['--flash-freq']!=frequencies.get(header[3]&15) or values['--flash-size']!='16MB' or header[3]>>4!=4:
        raise RuntimeError('Parametri flash diversi dall’header del bootloader: scrittura annullata.')
    return args

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--port');parser.add_argument('--flash',action='store_true')
    parser.add_argument('--expected-mac',default=EXPECTED)
    args=parser.parse_args()
    meta=json.loads((ROOT/'firmware/version.json').read_text())
    names=['CasaMonitor.ino.bootloader.bin','CasaMonitor.ino.partitions.bin','CasaMonitor.ino.bin']
    for name in names:
        if not (OUT/name).is_file():sys.exit('Build assente: eseguire prima firmware/scripts/build.py')
    settings=flash_settings(ROOT/".local/firmware-build/flash_args",OUT/names[0])
    binary=(OUT/names[2]).read_bytes()
    marker=f'CASA_RELEASE:{meta["board"]}:{meta["version_code"]}:{meta["version"]}'.encode()+b'\0'
    if marker not in binary:sys.exit('Versione della build differente dai metadati: ricompilare.')
    print(f'Piano USB: CasaMonitor {meta["version"]}; backup completo prima della scrittura; NVS Wi-Fi/Shelly conservata.')
    print('Parametri esatti della build:', ' '.join(settings))
    print('Scrittura: bootloader, tabella identica, selezione OTA iniziale e applicazione; nessun erase-flash completo.')
    if not args.flash:
        print('Nessun accesso USB. Per aggiornare: aggiungere --flash --port /dev/cu.usbmodem…');return
    if not args.port:sys.exit('Specificare esplicitamente --port prima di scrivere.')
    esptool=sorted((TOOLS/'tools/esptool_py').glob('*/esptool'))[-1]
    bootapp=TOOLS/'hardware/esp32/3.3.11/tools/partitions/boot_app0.bin'
    common=[str(esptool),'--chip','esp32s3','--port',args.port,'--baud','115200','--after','no-reset']
    def command(*parts):
        result=subprocess.run(common+[str(x) for x in parts],capture_output=True)
        if result.returncode:raise RuntimeError('Operazione esptool fallita; firmware non confermato. Nessuna cancellazione completa eseguita.')
        return result.stdout.decode(errors='replace')
    identity=command('read-mac').lower()
    if args.expected_mac.lower() not in identity:sys.exit('MAC differente dal display atteso: scrittura annullata.')
    stamp=time.strftime('%Y%m%d-%H%M%S');dest=ROOT/'.local/device-backups'/args.expected_mac.replace(':','')/stamp
    dest.mkdir(parents=True,mode=0o700);os.chmod(dest,0o700)
    def private(name):
        path=dest/name;fd=os.open(path,os.O_CREAT|os.O_EXCL|os.O_WRONLY,0o600);os.close(fd);return path
    table=private('partitions.bin');command('read-flash','0x8000','0x1000',table)
    if not table.read_bytes().startswith((OUT/names[1]).read_bytes()):sys.exit('Tabella partizioni differente. Scrittura annullata per conservare i dati.')
    backup=private('before-usb-16mb.bin');print('Lettura del backup completo, attendere…',flush=True)
    command('read-flash','0x0','0x1000000',backup)
    if backup.stat().st_size!=0x1000000:sys.exit('Backup incompleto: nessuna scrittura.')
    report=dict(version=meta['version'],mac=args.expected_mac,backup_sha256=hashlib.sha256(backup.read_bytes()).hexdigest(),firmware_sha256=hashlib.sha256(binary).hexdigest(),flashed=False)
    log=private('deployment.json');log.write_text(json.dumps(report,indent=2)+'\n')
    print('Backup completo. Caricamento con verifica hash esptool…',flush=True)
    common[common.index('--after')+1]='hard-reset'
    command('write-flash',*settings,'0x0',OUT/names[0],'0x8000',OUT/names[1],'0xe000',bootapp,'0x10000',OUT/names[2])
    report['flashed']=True;log.write_text(json.dumps(report,indent=2)+'\n')
    print('Scrittura e verifica riuscite. Configurazioni NVS conservate. Backup privato:',dest)
if __name__=='__main__':
    try:main()
    except (RuntimeError,OSError,subprocess.SubprocessError) as exc:sys.exit(str(exc))
