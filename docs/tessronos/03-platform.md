# 第3章　動作環境

## 3.1 対象のマシン

TessronOSが動くのは、Raspberry Pi 5、Raspberry Pi Compute Module 5(以下CM5)、Raspberry Pi 4 Model B、Raspberry Pi 400、Raspberry Pi Compute Module 4(以下CM4)、QEMUの`virt`マシンである。いずれもAArch64のCPUを4つ持ち、同じ`kernel/sysdepend/cpu/core/armv8a/`のコードで動く。マシンごとに異なるのはアドレスと割込み番号のテーブル、起動手順、一部のドライバだけで、`kernel/sysdepend/rpi5/`、`kernel/sysdepend/rpi4/`、`kernel/sysdepend/qemu_virt/`に分けてある。2つのRaspberry Piに共通のもの(メールボックス、ファームウェアの時計、乱数)は`kernel/sysdepend/rpi/`にある。CM5はRaspberry Pi 5と同じBCM2712とRP1を載せたモジュールで、Raspberry Pi 5と同じカーネルのイメージ(`TARGET=_RPI5_`)で動く。両者の違いは起動時にボードを見分けて吸収する(3.5)。Raspberry Pi 4、400、CM4はBCM2711を載せたボードで、命令セットの基準が違うので別のカーネルのイメージ(`TARGET=_RPI4_`)で動く(3.6)。

| 項目 | Raspberry Pi 5 | Raspberry Pi 4系 | QEMU virt |
| --- | --- | --- | --- |
| CPU | Cortex-A76 ×4(BCM2712) | Cortex-A72 ×4(BCM2711) | `-cpu cortex-a76 -smp 4` |
| RAM | 物理0から。1GB直下にファームウェアの予約がある | 物理0から。1GB未満、1GBから`0xFC000000`まで、8GBの版では4GBから8GBまでの範囲(3.6.1) | `0x40000000`から連続 |
| カーネルの配置 | `0x200000`(フラットな`tessronos.img`) | `0x200000`(フラットな`tessronos.img`) | `0x40200000`(ELFを`-kernel`で渡す) |
| 起動時のEL | EL2 | EL2 | EL1(`virtualization=on`でEL2) |
| 割込み | GIC-400(GICv2) | GIC-400(GICv2) | GICv2(`gic-version=2`) |
| コンソール | デバッグ用UART(PL011) | 40ピンヘッダのUART0(PL011) | PL011 |
| ディスク | SDカード、USBメモリ | SDカードかeMMC(EMMC2)、USBメモリ | virtio-blk、SDHCI、USBメモリ |
| ネットワーク | RP1のGigabit Ethernet | GENETのGigabit Ethernet | virtio-net |
| 画面 | ファームウェアが用意するフレームバッファ | ファームウェアが用意するフレームバッファ | `bochs-display` |
| USB | RP1のxHCI | PCIeの先のVL805(xHCI) | `qemu-xhci` |
| 時計 | ファームウェアに問い合わせる(電池がなければ1970年から) | ない。SNTPで合わせる | PL031 |
| 乱数 | ハードウェア乱数生成器とソフトウェアの疑似乱数生成器 | 同じ | ソフトウェアの疑似乱数生成器 |

Raspberry Pi 5のUSB、Ethernet、GPIO、サウンドは、PCIeの先のRP1というチップにある。TessronOSはPCIeのルートコンプレックスを自前で初期化し、RP1のアドレスウインドウを通してこれらを扱う。QEMUにはRP1がないので、同じ役割をvirtioデバイス、`qemu-xhci`、`bochs-display`が担う。

## 3.2 起動

Raspberry Pi 5では、EEPROMのブートローダがSDカードの第1パーティションから`config.txt`、デバイスツリー、`tessronos.img`を読み込み、CPU0をEL2でイメージの先頭から実行する。CM5では同じことを、eMMCかCM5 LiteのIOボードのSDカードから行う。`config.txt`は次のとおりである。

```text
kernel=tessronos.img
kernel_address=0x200000
device_tree=bcm2712-rpi-5-b.dtb
enable_uart=1
uart_2ndstage=1
pciex4_reset=0
framebuffer_depth=32
framebuffer_ignore_alpha=1

[cm5]
device_tree=bcm2712-rpi-cm5-cm5io.dtb
enable_rp1_uart=1
cmdline=cmdline-cm5.txt
[all]
[board-type=0x1a]
device_tree=bcm2712-rpi-cm5l-cm5io.dtb
[all]
```

`[cm5]`の節はCM5とCM5 Liteの両方が読み、`[board-type=0x1a]`の節はCM5 Liteだけが読む(0x1aはリビジョンコードのボードの種類)。後から読んだ`device_tree`が前の指定を置き換えるので、ボードごとに合ったデバイスツリーが読み込まれる。`[pi5]`の条件はCM5にも当てはまるので使わない。`enable_rp1_uart`と`cmdline`はCM5のコンソールを40ピンヘッダに移すための指定である(3.5.2)。

ファームウェアは、`kernel_address`の指定があっても、ヘッダのないイメージを`0x80000`に置くことがある。そのためカーネルのエントリは、実際にロードされたアドレスがリンクアドレスと異なれば、イメージを`0x200000`へコピーしてから処理を続ける。その後EL2からEL1へ移り、MMUを有効にして`main()`へ進む。2つめ以降のCPUはPSCIの`CPU_ON`で起動する。

起動の後半では、ドライバ、描画、ウインドウ、USB、ネットワーク、乱数、プロセス管理、実身マネージャの順に初期化し、最後に初期プログラム(`CNF_INIT_PATH`)があればそれを実行する。デスクトップ構成では、続いてデスクトップが起動する。

## 3.3 SDカードの構成

SDカードのイメージは`make TARGET=_RPI5_ BUILD_ID=<名前> sdimg`で作る。パーティションテーブルはGPTで、パーティションは次のとおりである。

| パーティション | 形式 | 内容 |
| --- | --- | --- |
| 1 | FAT32、256MB | `config.txt`、`cmdline-cm5.txt`、3つのデバイスツリー、`tessronos.img`と、`/boot`に置くファイル |
| 2 | TSFS、64MB | システムボリューム。フォント、壁紙、定義実身、小物、辞書を実身として格納する |
| 3 | 空、16MB | カーネル内の試験の構成にだけある作業用パーティション |

デバイスツリーはRaspberry Pi 5の`bcm2712-rpi-5-b.dtb`、CM5の`bcm2712-rpi-cm5-cm5io.dtb`、CM5 Liteの`bcm2712-rpi-cm5l-cm5io.dtb`で、ファームウェアの配布物から取得して使う。再配布はしないので、リポジトリには含めていない。1つのイメージがRaspberry Pi 5とCM5の両方で起動するので、同じイメージをSDカードにもCM5のeMMCにも書き込める。

Raspberry Pi 4系のイメージは`make TARGET=_RPI4_ BUILD_ID=<名前> sdimg`で作る。構成は同じで、ファイルが違う(3.6.5)。

## 3.4 動作確認の範囲

Raspberry Pi 4系は実機ではまだ確かめておらず、QEMUの`raspi4b`で確かめられる範囲を確かめた(3.6.6)。

| 項目 | QEMU | Raspberry Pi 5 |
| --- | --- | --- |
| カーネル内の試験 | すべて(449件、前提条件を満たさない10件を除く) | 391件中390件(2026年9月28日) |
| 4つのCPU、SDカード | 確認済み | 確認済み |
| USBのキーボードとマウス、USBメモリ | 確認済み | マウスまで確認済み |
| Ethernet、DHCP、DNS | 確認済み | 確認済み |
| IPv6(ルータ広告による自動設定、AAAA、IPv6のTCP) | 確認済み(QEMUのユーザモードのネットワーク) | 未確認 |
| SNTPによる時計合わせ | 確認済み | 未確認 |
| デスクトップ | 確認済み | イメージは作成できるが、実機での確認はこれから |
| サウンド | USBオーディオで確認済み(出力先の選択と抜き差しによる切替えを含む) | 未確認(I2SとHDMIの音は実機の試験をこれから行う) |

CM5は実機ではまだ確認していない。ボードの見分けとボードごとの値はカーネル内の試験(`ktest_board`)で、eMMCの初期化と読み書きはQEMUのeMMC(`make SD_EMMC=1`)で確かめた(3.5.4)。

## 3.5 Raspberry Pi Compute Module 5

CM5はRaspberry Pi 5と同じBCM2712とRP1を載せたモジュールで、IOボード(CM5IO)に挿して使う。eMMC(16GB、32GB、64GB)を載せたものと、載せていないCM5 Liteがある。周辺機器のアドレスと割込み番号はRaspberry Pi 5と同じなので、カーネルのイメージは分けず、起動時にボードを見分けて、違う配線だけをボードの表で切り替える。この節の値は、ファームウェアの配布物の`bcm2712-rpi-5-b.dtb`、`bcm2712-rpi-cm5-cm5io.dtb`、`bcm2712-rpi-cm5l-cm5io.dtb`と、CM5とCM5IOのデータシートで確かめたものである。

### 3.5.1 差異

| 項目 | Raspberry Pi 5 | CM5(eMMCあり) | CM5 Lite | 扱い |
| --- | --- | --- | --- | --- |
| ルートノードの`compatible` | `raspberrypi,5-model-b` | `raspberrypi,5-compute-module` | CM5と同じ(`model`の末尾が`Lite`) | ボードの見分けに使う |
| リビジョンコードのボードの種類 | 0x17 | 0x18 | 0x1a | 見分けを確定する |
| デバイスツリー | `bcm2712-rpi-5-b.dtb` | `bcm2712-rpi-cm5-cm5io.dtb` | `bcm2712-rpi-cm5l-cm5io.dtb` | `config.txt`の条件で選ぶ(3.2) |
| SDHCI(`0x10_00ff_f000`、SPI 273)の先 | SDカードのスロット、4ビット | モジュール上のeMMC、8ビット、HS200/HS400対応、取外し不可 | IOボードのmicroSDスロット、4ビット | eMMCはMMCとして初期化する(3.5.3) |
| カード検出 | 常時オンのGPIO 5(負論理) | なし(`broken-cd`) | なし(`broken-cd`) | CM5では検出線を見ず、カードを初期化してみる |
| SDの信号電圧 | 3.3Vと1.8Vを常時オンのGPIO 3で切り替える | 1.8V固定 | Raspberry Pi 5と同じ | CM5では1.8Vの信号であることをコントローラに設定する |
| 常時オンのGPIO 5、6 | 5はカード検出、6はSDの電源の異常 | Wi-Fiのアンテナの選択(5は出力H、6は出力L) | CM5と同じ | CM5では5と6をボードのものとし、GPIOの実身から変更させない |
| ACT LED | 常時オンのGPIO 9(負論理) | 同じ | 同じ | 変更なし |
| 電源LED | RP1のGPIO 44 | 同じ | 同じ | 変更なし |
| デバッグ用UART(`0x10_7d00_1000`) | 3ピンのコネクタ | モジュール上にコネクタを付ける場所とテストポイントだけがあり、CM5IOにはコネクタがない | CM5と同じ | 起動の前半の出力はここに出る |
| 40ピンヘッダのUART0(GPIO 14、15) | RP1のUART0 | 同じ | 同じ | CM5ではコンソールをここに移す(3.5.2) |
| Ethernet | RP1のGEM、BCM54213PE、MDIOのアドレス1 | RP1のGEM、BCM54210PE、MDIOのアドレス0。PHYの割込みがRP1のGPIO 37 | CM5と同じ | PHYは標準のレジスタだけで扱う。ボードの表のアドレスを先に試し、応答がなければ全アドレスを探す |
| USB | RP1のxHCI 2つ | 同じ(CM5IOのType-Aのポート2つ。Type-CのポートはrpibootとUSBデバイス用) | 同じ | 変更なし。CM5には過電流の検出線(RP1のGPIO 43)がない |
| PCIeの外部コネクタ | FFCのコネクタ(PCIe 2.0 x1) | CM5IOのM.2 Mキー(PCIe 2.0 x1) | 同じ | どちらもまだ使わない |
| ファン | RP1のGPIO 45(PWM)とGPIO 29(回転数) | 同じ(CM5IOの4ピンのコネクタ) | 同じ | どちらもまだ制御しない |
| 電源ボタン | GPIO 20 | 同じ(CM5IOのボタン) | 同じ | 変更なし |
| RTC | ファームウェアに問い合わせる。電池は別売 | 同じ。CM5IOにCR2032のソケットがある | 同じ | 変更なし。充電はしない(デバイスツリーの`trickle-charge-microvolt`は0) |

ボードは次の順に見分ける。起動の最初に、デバイスツリーのルートノードの`compatible`と`model`からボードを決める(`kernel/sysdepend/cpu/core/armv8a/board.c`)。続いてデバイスの初期化の最初に、ファームウェアのプロパティインタフェースでボードのリビジョン(タグ`0x00010002`)を問い合わせ、そのボードの種類が表にあればそれで確定する。デバイスツリーは`config.txt`で選んだファイルにすぎないが、リビジョンはボードそのものを表すためである。どちらでも分からないボードはRaspberry Pi 5として扱い、名前にはデバイスツリーの`model`を使う。見分けた結果は`knl_board`(`include/ts/board.h`)にあり、SDHCI、GPIOの実身、Ethernet、システムの実身(`BOARD`と`BOARD_REV`)が参照する。

ボードを見分けた直後に、ファームウェアにCPUのクロックの最大値(タグ`0x00030004`)と最小値(タグ`0x00030007`)を問い合わせ、最大値と既定の上限1800MHzの低いほうに設定する(タグ`0x00038002`)。ファームウェアはCPUを定格より低いクロックで起動し(Raspberry Pi 5では1.5GHz)、上げるのはOSに任せるためである。設定の実身が読めるようになると、マシンの設定の上限`CPU_MHZ`に合わせ直す(3.9)。設定した結果はコンソールに`TessronOS: processors at ... MHz`と出る。温度が上がりすぎたときや電源の電圧が下がったときにクロックを下げるのは、引き続きファームウェアが行う。Raspberry Pi 4、400、CM4でも同じ処理を行う。

BCM2712にはC1とD0の2つの版があり、ファームウェアの配布物の`bcm2712d0-rpi-5-b.dtb`によれば、D0ではデバッグ用UARTの割込みがSPI 120(C1は121)、常時オンのGPIOのバンク0が15本(C1は17本)である。カーネルはC1の値を使っているので、D0の版のボードではデバッグ用UARTの受信の割込みが届かない(受信したバイトは読出しのときにFIFOから取り出すので、入力は遅れるが失われない)。これはRaspberry Pi 5の2GBの版にも共通する未対応の点である。

### 3.5.2 コンソール

CM5IOにはデバッグ用UARTのコネクタがないので、CM5ではコンソールを40ピンヘッダの8番(TXD)と10番(RXD)、つまりRP1のUART0に移す。ファームウェアがRP1のUART0にクロックを入れるのは`config.txt`に`enable_rp1_uart=1`があるときだけで、クロックのないUART0に触れるとSErrorになる。そこで、移すかどうかをカーネルのイメージには埋め込まず、`config.txt`の`[cm5]`の節で`enable_rp1_uart=1`と`cmdline=cmdline-cm5.txt`を組にして指定する。`cmdline-cm5.txt`の`tessronos.console=rp1`は、ファームウェアがデバイスツリーの`/chosen/bootargs`に入れて渡す。カーネルはこれを見て、RP1のGPIOを初期化した直後にコンソールを移す。それまでの出力はデバッグ用UARTに出る。Raspberry Pi 5でも、`cmdline.txt`に`tessronos.console=rp1`を書き、`config.txt`に`enable_rp1_uart=1`を足せば、ビルドし直さずに同じことができる(`make RP1_CONSOLE=1`と同じ結果になる)。

### 3.5.3 eMMC

`device/sd/sdhci.c`は、SDカードとして応答しないデバイスをMMC(JEDECのeMMC)として初期化する。eMMCはアイドル状態でCMD8に応答しないので、SDカードとの区別はCMD8の無応答で行う。初期化の手順は次のとおりである。

1. CMD0でアイドル状態にし、CMD1(引数`0x40FF8080`。セクタアドレスと、2.7〜3.6Vおよび1.70〜1.95V)をビジーが解けるまで繰り返す。応答のアクセスモードがセクタなら、2GBを超えるデバイスとしてセクタ番号で読み書きする。
2. CMD2でCIDを読み、CMD3で相対アドレス(1)を与え、CMD9でCSDを読む。書込み禁止のビットはSDカードと同じ位置にある。
3. CMD7で選択し、クロックを25MHz(後方互換のタイミングの上限26MHzの内側)に上げ、CMD8で拡張CSDを読む。2GBを超えるデバイスの大きさは拡張CSDの`SEC_COUNT`(212〜215バイト目)から求める。
4. CMD6で拡張CSDの`BUS_WIDTH`(183バイト目)を書き換えてバス幅を広げる。ボードが8本を配線し(CM5)、コントローラの能力レジスタのビット18が8ビットに対応していれば8ビット、そうでなければ4ビットを試す。切り替えた後に拡張CSDをもう一度読み、デバイスを記述する192バイト目以降が最初に読んだものと一致すれば、その幅を使う。一致しなければ1ビットに戻して次の幅を試す。

データの読み書き(CMD17、18、24、25と自動CMD12)、DMAの方式の選択と検査、パーティションの読み取りはSDカードと共通である。eMMCは取外しできないディスクとして`sda`に登録する。HS200とHS400への切り替え、ブートパーティションとRPMBへのアクセス、eMMCのキャッシュの利用はしていない。

### 3.5.4 試験

QEMUにはCM5の機種がないので、次の2つで確かめる。

- `ktest_board`は、Raspberry Pi 5、CM5、CM5 Lite、QEMU、未知のボードのルートノードを持つデバイスツリーをメモリ上に組み立て、ルートノードの読み取り、`compatible`と`model`とリビジョンからの見分け、ボードごとの値、コマンドラインの解析を確かめる。
- `make TARGET=_QEMU_VIRT_ KTEST=1 KTONLY=sd SD_EMMC=1 test`は、試験用のSDカードのイメージをQEMUの`emmc`としてsdhci-pciに挿し、能力レジスタに8ビットのビットを足して、`ktest_sd`をそのまま通す。xPackのQEMU 9.2の`emmc`は`-device`で挿せないので、WindowsのQEMU(11.0で確認)を使う。

## 3.6 Raspberry Pi 4、Raspberry Pi 400、Compute Module 4

Raspberry Pi 4 Model B、Raspberry Pi 400、Compute Module 4(以下CM4)は、BCM2711を載せたボードである。CPUがARMv8.0-AのCortex-A72なので、ARMv8.2-Aで作るRaspberry Pi 5のイメージとは別に、`make TARGET=_RPI4_`でカーネルのイメージを作る。3つのボードは同じイメージで動き、ボードの違いはCM5と同じ仕組み(3.5.1)で起動時に見分けて吸収する。アドレスと割込み番号は、ファームウェアの配布物の`bcm2711-rpi-4-b.dtb`、`bcm2711-rpi-400.dtb`、`bcm2711-rpi-cm4.dtb`とBCM2711のペリフェラルの資料で確かめたもので、`include/sys/sysdepend/cpu/bcm2711/sysdef.h`にある。ボードに依存するコードは`kernel/sysdepend/rpi4/`に、Raspberry Pi 5と共通のメールボックス、時計、乱数は`kernel/sysdepend/rpi/`にある。

実機ではまだ確かめていない。QEMUの`raspi4b`で確かめられる範囲は確かめ(3.6.6)、PCIe、VL805、GENET、GPIOのように実機でしか確かめられないものは、資料とデバイスツリーの値に基づいて書いた。

### 3.6.1 差異

| 項目 | Raspberry Pi 5 | Raspberry Pi 4系 | 扱い |
| --- | --- | --- | --- |
| CPU | Cortex-A76 ×4。ARMv8.2-Aで、LSEのアトミック命令と暗号拡張がある | Cortex-A72 ×4。ARMv8.0-Aで、LSEのアトミック命令も暗号拡張もない(CRC32はある) | 命令セットの基準を`-march=armv8-a+crc`にした別のイメージを作る(3.6.2) |
| 起動 | EEPROMのブートローダがカーネルを読み込む | EEPROMのブートローダがSDカードの`start4.elf`と`fixup4.dat`を読み込み、それがカーネルを読み込む | SDカードのFATパーティションにファームウェアのファイルを置く(3.6.5) |
| 起動時のEL | EL2 | EL2(ファームウェアに組み込まれたarmstubが入れる) | `boot.S`のまま |
| 2つめ以降のCPU | PSCIの`CPU_ON`(TF-A) | スピンテーブル。CPU1〜3がそれぞれ物理`0xe0`、`0xe8`、`0xf0`を見て待つ。PSCIはない | デバイスツリーの`enable-method`でPSCIとスピンテーブルを選ぶ(3.6.3) |
| 汎用タイマ | 54MHz | 54MHz | `CNTFRQ_EL0`を読む |
| RAM | 物理0から。1GB直下にファームウェアの領域 | 1、2、4、8GB。物理0からGPUの領域の手前まで、`0x40000000`から`0xFC000000`まで、8GBではさらに`0x1_00000000`から`0x2_00000000`まで。`0x0`〜`0x1000`はarmstubとスピンテーブル | 範囲はデバイスツリーの`/memory`と`/memreserve/`から取る |
| 周辺機器 | `0x10_0000_0000`以降と、PCIeの先のRP1のアドレスウインドウ | `0xFC000000`〜`0xFF7FFFFF`、ARMのローカル周辺機器は`0xFF800000`以降(low peripheralモード) | RAMの4つめの1GBに周辺機器があるので、RAMが1GBを埋めないところは2MB単位でリニアマップに載せる |
| DMAの届く範囲 | RP1の先のバスマスタは内向きのアドレスウインドウを通してメモリ全体に届く | VideoCore(メールボックス、フレームバッファ)とEMMC2の初期の版(B0)は1GBまで、PCIeの先は3GBまで、GENETはメモリ全体 | DMAに使うページを1GB未満から取る(`DMA_ZONE_LIMIT`)。EMMC2のバスアドレスはデバイスツリーの`dma-ranges`から求める |
| 割込み | GIC-400 | GIC-400。ディストリビュータ`0xFF841000`、CPUインタフェース`0xFF842000` | GICのドライバのまま |
| コンソール | 3ピンのデバッグUART(PL011) | 40ピンヘッダの8番と10番のPL011(UART0、`0xFE201000`、SPI 121)。既定ではBluetoothが使っているので、`dtoverlay=disable-bt`で取り返す | USBシリアル変換器でつなぐ |
| SDカード | SDHCI(SPI 273) | EMMC2(`0xFE340000`、SPI 126) | `sdhci.c`を使う。基準クロックはメールボックスで問い合わせる |
| USB | RP1のxHCI 2つ | PCIeの先のVL805(xHCI、4ポート)。ルートコンプレックスは`0xFD500000`、INTAはSPI 143。USB-CのポートのDWC2は使わない | ルートコンプレックスをカーネルが初期化し、VL805のプログラムの読込みをメールボックスで依頼する。xHCIから上のUSBスタックは共通 |
| Ethernet | RP1のCadence GEMとBCM54213PE | GENET v5(`0xFD580000`、SPI 157と158)とBCM54213PE(RGMII) | GENETのドライバ`eth_genet.c`を新しく作った。PHYは標準のレジスタとBroadcomのクロック遅延のレジスタだけで扱う |
| 画面 | ファームウェアのフレームバッファ | 同じ(KMSのオーバーレイは読み込ませない) | `disp_rpi5.c`を両方で使う |
| サウンド | USBオーディオ、RP1のI2S、HDMIの音声(6.5) | USBオーディオ。ほかにI2S、PWMによるアナログ出力、HDMIの音声がある | USBオーディオだけにする |
| GPIO | 常時オンのGPIOとRP1のGPIO | BCM2711のGPIO(`0xFE200000`、58本)。ACT LEDはGPIO 42 | `gpio_bcm2711.c`と、GPIOの実身のバンクの表(3.6.4) |
| GPU | V3D 7.1 | V3D 4.2 | 対象外 |
| 時計 | ファームウェアに問い合わせる | RTCはない | SNTPで合わせる |
| 乱数 | iproc-rng200 | iproc-rng200(`0xFE104000`) | 同じドライバを使う |
| 再起動と電源断 | PSCI | PSCIはない | 再起動はPMのウォッチドッグで行う。電源断の代わりにファームウェアの停止状態に入る(3.6.3) |

ボードは次のとおり見分ける。どれもSDHCIの先にカード検出線がない(デバイスツリーの`broken-cd`)ので、カードを初期化してみて見つける。

| ボード | ルートノードの`compatible` | リビジョンコードのボードの種類 | ボードの表の値 |
| --- | --- | --- | --- |
| Raspberry Pi 4 Model B | `raspberrypi,4-model-b` | 0x11 | SDカード4ビット、PHYのMDIOのアドレス1 |
| Raspberry Pi 400 | `raspberrypi,400` | 0x13 | 4Bと同じ。GPIO 42のLEDは電源LEDなので、点滅の後は点けたままにする |
| CM4(eMMCあり、Lite) | `raspberrypi,4-compute-module` | 0x14 | eMMCは8ビット(LiteではIOボードのSDカード)、PHYのMDIOのアドレス0 |

### 3.6.2 命令セットと外部の生成物

`TARGET=_RPI4_`では、カーネル、プログラム、ライブラリのすべてを`-march=armv8-a+crc -mno-outline-atomics`でコンパイルする。ARMv8.0-AではGCCのアトミック操作の既定がlibgccの関数(`__aarch64_cas4_acq_rel`など)の呼出しになり、libgccをリンクしない小物がリンクできないので、排他ロードとストアの命令で直に作らせる。プログラム、実身のレコード、それらがリンクするライブラリはRaspberry Pi 5のものと混ざらないように`obj/v80/`に置く(`build_make/common.mk`の`UOBJ`)。

外部で作るオブジェクトも同じ基準で別に作る。NetBSDのスタックは`MARCH=armv8-a+crc OUT=~/tessronos-netbsd-v80 bash tools/netbsd/build.sh all`で、Mozcは`MARCH=armv8-a+crc OUT=~/tessronos-mozc-v80 bash tools/mozc/build.sh all`で作る。`TARGET=_RPI4_`のときmakeはこの2つの置き場を見る。NetBSDのスタックはリンクの前になければmakeが作る。ブラウザ(Blink)はARMv8.2-Aでしか作っていないので、`BROWSER=1`はエラーにする。

ARMv8.0-Aにない命令が残っていないことは`tools/chk_isa.sh`で確かめる。オブジェクトやイメージの命令を一つずつ調べ、LSEのアトミック命令、`ldapr`、内積、暗号拡張の命令を探す。libgccのアトミック操作の関数(`__aarch64_cas4_acq`など)は両方の形を持って実行時に選び、このシステムでは常にLSEでない方を選ぶので、別に数える。NetBSDのスタックとMozcのARMv8.0-Aの版、Raspberry Pi 4のイメージには見つからない(ARMv8.2-AのMozcには352個ある)。QEMUのCortex-A72はLSEの命令を持たないので、残っていればQEMUでも未定義命令の例外になる。暗号拡張はQEMUのCortex-A72が持つと答えるので、確かめられるのはこの検査だけである。

### 3.6.3 起動とメモリ

ファームウェアはイメージを`0x80000`に置き、EL2でCPU0だけを実行する。`boot.S`は、イメージをリンクアドレス`0x200000`へコピーする前に、`MIDR_EL1`の部品番号がそのイメージの対象のCPU(Raspberry Pi 4のイメージではCortex-A72の0xD08、Raspberry Pi 5のイメージではCortex-A76の0xD0B)であることを確かめる。違えば、もう一方のボードのコンソール(Raspberry Pi 4のイメージはRaspberry Pi 5のデバッグ用UART、Raspberry Pi 5のイメージはRaspberry Pi 4のUART0)に、このイメージがどのボード向けかを1行書いて止まる。続いて`board.c`が、デバイスツリーのルートノードの`compatible`にそのイメージのSoC(`brcm,bcm2711`か`brcm,bcm2712`)があることを確かめ、なければ同じように止まる。

イメージは`.data`までがフラットなイメージの範囲で、`boot.S`はその範囲をコピーする。リンカが`.data`の後ろに置いていたGOT(NetBSDのスタックがリンクセットの終わりを参照する)がコピーされず、コピーした先でゼロのままになって、NetBSDのスタックの初期化がNULLを参照して止まっていた。リンカスクリプトでGOTを`.data`に入れて直した。イメージが`0x80000`に置かれるRaspberry Pi 5でも同じことが起きていた。

2つめ以降のCPUは、デバイスツリーの`/cpus/cpu@*`の`enable-method`が`spin-table`なら、`smp.c`がその`cpu-release-addr`にエントリの物理アドレスを書き、キャッシュをフラッシュして`sev`で起こす。ファームウェアのスピンテーブルは引数を渡さないので、エントリ(`knl_secondary_spin_entry`)で`MPIDR_EL1`のAff0をプロセッサの番号とし、Aff0がデバイスツリーの並びと一致しないCPUは起こさない。

メモリの範囲はデバイスツリーの`/memory`から取り、ヘッダの`/memreserve/`の範囲(`0x0`〜`0x1000`)はページアロケータに渡さない。リニアマップは1GBのブロックで作っていたが、RAMが1GBを埋めないところは2MBのブロックの表(`mmu.c`の`edge_l2`)で、RAMのある部分だけを載せる。`0xC0000000`〜`0xFC000000`のRAMの後ろの周辺機器を通常のメモリとして載せると、CPUが先読みでレジスタを読むことがあるためである。DMAに使うページのゾーンは1GB未満とし(`DMA_ZONE_LIMIT`)、VideoCore、EMMC2、PCIeのどれもが届くページを`KAF_DMA32`で取る。

再起動は、PMのウォッチドッグに数ティック後の全体リセットを設定して行う。電源を切る手段はないので、システムの終了では、リセットの状態のレジスタにブートパーティション63を書いてからリセットする。ファームウェアはそれを停止の指示として受け取り、CPUを止めたまま電源の入れ直しを待つ。

### 3.6.4 デバイス

| デバイス | ドライバ | 内容 |
| --- | --- | --- |
| コンソール | `device/ser/ser.c` | UART0(PL011)。クロックはメールボックスで問い合わせ、答えがなければ48MHz |
| メールボックス、時計、乱数 | `kernel/sysdepend/rpi/` | Raspberry Pi 5と共通。時計のタグには答えがないのでSNTPで合わせる。乱数生成器はデバイスツリーのノードが無効なら使わない |
| 画面 | `device/disp/disp_rpi5.c` | ファームウェアが用意するフレームバッファ |
| SDカード、eMMC | `device/sd/sdhci.c` | EMMC2。下を参照 |
| GPIO | `device/gpio/gpio_bcm2711.c` | 58本を1つのバンク0にGPIOの番号で並べる。プルの設定は読み返せる |
| PCIe | `kernel/sysdepend/rpi4/pcie_rc.c` | ルートコンプレックスを初期化し、`kernel/sysman/pcie.c`の走査に設定空間の読み書きを渡す。下を参照 |
| USB | `device/usb/` | PCIeの先のxHCI。INTA(SPI 143)で割込みを受ける。`make PCIE_POLL=1`で一定の間隔で見る |
| Ethernet | `device/net/eth_genet.c` | GENET v5。下を参照 |

EMMC2の基準クロックは能力レジスタに書かれていないので、メールボックスでEMMC2のクロック(番号12)の周波数を問い合わせる。EMMC2が見るメモリは、デバイスツリーの`/emmc2bus`の`dma-ranges`で求める。初期の版(B0)では物理アドレスの1GBまでがバスアドレス`0xC0000000`からに見え、後の版(C0)ではファームウェアがこれを書き換え、周辺機器の手前までがそのままのアドレスで見える。`dma-ranges`がなければB0の値を使う。届かないバッファはバウンスバッファ(256KB、DMAのゾーンから取る)を通す。ADMA2の記述子の表もバスアドレスで渡す。CM4ではボードの表の8本のデータ線を使ってeMMCを8ビットのバスに切り替える。信号電圧はファームウェアが3.3Vのまま渡すので、どのボードでも1.8Vには切り替えない。

GPIOの実身は、BCM2711のピンを3つのバンクにして見せる。`gpio-0`は40ピンヘッダのGPIO 0〜27、`gpio-1`はGPIO 28〜45、`gpio-2`はGPIO 46〜57である。ピンはGPIOの番号で書く。コンソールのUART0のピン(14、15)と、ボードの配線(EthernetのPHYのMDIOとRGMII、BluetoothのUART、Wi-FiのSDIO、オーディオのPWM、SPIフラッシュ)に使うGPIO 28〜57は、ACT LEDのGPIO 42を除いてカーネルのものとし、実身から変更しない。ACT LEDは高で点く。

PCIeのルートコンプレックスはファームウェアがリセットしたまま渡すので、カーネルが次の順に初期化する。ブリッジと先のデバイスをリセットに入れてからブリッジだけを出し、SerDesの電源を入れ、存在しない機能の設定空間の読出しにエラーではなく全ビット1を返させ、内向きのアドレスウインドウ(PCIeの0から物理0へ。レジスタが2のべき乗しか取らないので4GBとし、外向きのアドレスウインドウと重なる上の1GBを除いた3GBが届く)と外向きのアドレスウインドウ(物理`0x6_0000_0000`からPCIeの`0xC000_0000`へ1GB)を設定し、先のデバイスのリセットを解いてリンクを待つ。アドレスウインドウの値はデバイスツリーの`/scb/pcie@7d500000`の`ranges`と`dma-ranges`から取る。設定空間は、ルートポートはレジスタの先頭に、その下のバスの機能は`0x8000`の4KBのウインドウに、`0x9000`の索引のレジスタに番号を書いて見る。その後は`kernel/sysman/pcie.c`の共通の走査がバスに番号を付け、BARを外向きのアドレスウインドウに割り当てる。Raspberry Pi 4 Model B(ボードのリビジョン1.4以降)とRaspberry Pi 400はVL805のプログラムをVL805自身ではなくボードのEEPROMに持つので、リセットの後でVL805の設定空間の版のワード(0x50)が0なら、プロパティのタグ`NOTIFY_XHCI_RESET`(0x00030058)にVL805の設定空間のアドレスを渡し、ファームウェアに読み込ませる。CM4ではスロットに挿したカードが何かは分からないので、これをしない。汎用のxHCIのカードなら、VL805と同じ経路でUSBが使える。CM4のUSB-Cのポート(DWC2)には対応していない。

GENETは、システム、RGMII、割込み、受信バッファ、UniMAC、受信と送信のDMAの部分からなる。DMAはそれぞれ256個の記述子(状態と長さ、アドレスの下位と上位の3ワード)と17本のリングを持ち、既定のリング(16番)だけを、受信と送信に128個ずつの記述子で使う。リングは、作る側と使う側の16ビットのカウンタの組で、受信ではコントローラがバッファを埋めて数え、ドライバがフレームを取り出して数え返す。受信したフレームはバッファの2バイト目から始まる。PHYのBCM54213PEはGENET自身のMDIO(UniMACの`0x614`、デバイスツリーの`mdio@e14`)にあり、アドレスはボードの表の値から試し、答えがなければ全アドレスを探す。デバイスツリーの`phy-mode`が`rgmii-rxid`なので、受信のクロックはPHYで、送信のクロックはGENETで遅らせる。PHYの受信の遅延はBroadcomの補助制御レジスタ(0x18)のシャドウ7、送信の遅延はシャドウレジスタ(0x1c)のシャドウ3で設定する。MACアドレスは、ファームウェアがデバイスツリーに書く`local-mac-address`、メールボックスのボードのMACアドレスの順に取る。割込みは使わず、Raspberry Pi 5のドライバと同じく、読み手が受信のリングを見る。すべてのフレームを受け取り、要らないものはプロトコルスタックが捨てる。

### 3.6.5 SDカードのイメージ

`make TARGET=_RPI4_ BUILD_ID=<名前> sdimg`で`build_make/obj/_RPI4_<名前>/sd.img`を作る。デスクトップとMozcを含むイメージは`make TARGET=_RPI4_ DESKTOP=1 MOZC=1 BUILD_ID=pi4 sdimg`で作る。パーティションの構成はRaspberry Pi 5のイメージ(3.3)と同じで、パーティション1のFAT32に次のファイルを置く。

| ファイル | 内容 |
| --- | --- |
| `start4.elf`、`fixup4.dat` | Raspberry Pi 4系のファームウェア |
| `bcm2711-rpi-4-b.dtb`、`bcm2711-rpi-400.dtb`、`bcm2711-rpi-cm4.dtb` | デバイスツリー |
| `overlays/disable-bt.dtbo` | UART0をBluetoothから取り返すオーバーレイ |
| `config.txt` | `etc/boot/config-rpi4.txt` |
| `tessronos.img` | カーネル |

ファームウェア、デバイスツリー、オーバーレイは`make sdimg`のときにRaspberry Piのファームウェアの配布物から`build_make/obj/boot/`に取得する。これらはBroadcomのライセンスのもとでバイナリとしてだけ再配布できるものなので、リポジトリには含めない。`RPI4_FW=<ディレクトリ>`で手元のファームウェアの`boot`ディレクトリを指すこともできる。`tools/mksd.py`と`tools/mkfat.py`は、`overlays/disable-bt.dtbo=<ファイル>`のように`名前=ファイル`で名前を変え、名前の`/`の前をルートの下のディレクトリとして置く。

`config.txt`は次のとおりである。

```text
[pi4]
arm_64bit=1
kernel=tessronos.img
kernel_address=0x200000
device_tree=bcm2711-rpi-4-b.dtb
dtoverlay=disable-bt
enable_uart=1
uart_2ndstage=1
framebuffer_depth=32
framebuffer_ignore_alpha=1

[pi400]
device_tree=bcm2711-rpi-400.dtb

[cm4]
device_tree=bcm2711-rpi-cm4.dtb
[all]
```

`[pi4]`の条件はRaspberry Pi 4 Model B、400、CM4のすべてに当てはまり、Raspberry Pi 5には当てはまらない。このカードをRaspberry Pi 5に挿すと、ファームウェアはカーネルを見つけられずに止まる。後から読んだ`device_tree`が前の指定を置き換えるので、ボードごとに合ったデバイスツリーが読み込まれる。カーネルとデバイスツリーと`config.txt`だけを書き込んだカードの上で差し替えるには`make TARGET=_RPI4_ sd SD=<FATパーティション>`を使う。パーティションテーブルはGPTなので、GPTから起動できる版のEEPROMのブートローダ(2020年9月以降)が要る。

### 3.6.6 試験

QEMUの`-M raspi4b`(xPackの9.2で確認)は、GIC-400、PL011、メールボックスとフレームバッファ、EMMC2と古いEMMC、PMのウォッチドッグをエミュレートし、PCIe、GENET、乱数生成器を持たない。QEMUはデバイスツリーのそれらのノードを`disabled`にして渡すので、カーネルはそれを見て使わない(`knl_dtb_okay`)。CPUはEL2で始まり、2つめ以降のCPUはQEMUのスピンテーブルで待つ。QEMUはSDカードを古いEMMC(`0xFE300000`)に繋ぎ、EMMC2を空のままにするので、QEMUではコマンドラインの`tessronos.sd=emmc`でSDカードのドライバを古いEMMCに向ける。実機では古いEMMCはWi-Fiに繋がっているので、この指定はしない。

| makeのターゲット | 内容 |
| --- | --- |
| `make TARGET=_RPI4_ BUILD_ID=<名前> qemu-rpi4` | SDカードのイメージを作り、QEMUの`raspi4b`で起動する(Ctrl-A Xで終了) |
| `make TARGET=_RPI4_ KTEST=1 BUILD_ID=<名前> test-rpi4` | カーネル内の試験を走らせ、`KTEST SUMMARY`を待つ |

QEMUにはカーネルのイメージ`tessronos.img`を`-kernel`で、デバイスツリーを`-dtb`で、SDカードのイメージを2のべき乗の大きさに伸ばしたコピーを`-drive if=sd`で渡す。ファームウェアと同じく、イメージは`0x80000`に置かれてから`0x200000`へコピーされる。QEMUの`raspi4b`では、起動、4つのCPU、タイマ、割込み、コンソール、SDカード(ただし古いEMMCで、PIOでの転送)、画面、ウォッチドッグによるリセット、誤ったボードのイメージの検出(Raspberry Pi 5のイメージを`raspi4b`で起動すると、UART0にその旨の1行が出て止まる)を確かめた。RAMが1GBの途中で終わるときのリニアマップは、QEMU virtを`-m 1100M`で起動して確かめた。デスクトップとMozcの構成も起動し、かな漢字変換が始まるところまで確かめた。カーネル内の試験は、`kernel`の群が68件すべて、`fs`の群が134件中133件通った(2026年10月1日)。通らなかった1件(`test_calendar`)は、試験の前にシステムの時差が設定されていると失敗するもので、QEMU virtでも同じ条件で失敗する。試験の`#ifdef RPI5`は、「QEMUではなくボード」の意味のものを`KT_BOARD`(Raspberry Pi 5か4のとき1)に置き換えた。

デスクトップの構成で起動すると、起動の後でデバイスの実身とボードの確認(`kernel/sysdepend/rpi4/objtest.c`、`objtest:`の行)が走る。1枚のカードで多くを確かめられるように、Raspberry Pi 5の確認(バンク、LED、カーネルのピン、入力の変化、温度とクロック、画面、USBの実身、乱数)に、ボード(4つのCPU、メモリの範囲、DMAのゾーン)、SDカードの転送の方式、PCIeのリンクとその先の機能、Ethernetのリンクを加えた。カーネル内の試験の構成では走らせない。実機では次の項目を確かめる。

| 項目 | 確かめ方 | 期待する結果 |
| --- | --- | --- |
| 起動、ボードの見分け | シリアルコンソール | `board Raspberry Pi 4 Model B`(400、CM4ではそれぞれの名前)、リビジョンが表示される |
| メモリ | 起動時の`RAM[n]`の行と`objtest: board` | 8GBでは3つか4つの範囲。`0xFC000000`より上を読み書きして止まらない |
| 4つのCPU | `4 of 4 CPUs online` | スピンテーブルから4つとも起きる |
| SDカード | `sda`の行と`objtest: sd` | ADMA2かSDMAで転送している(`data moved by`がデータポートでない)。B0とC0の両方の基板で`sd EMMC2 sees memory`の値がデバイスツリーと合う |
| CM4のeMMC | `sda eMMC ... 8 data lines` | 8ビットに切り替わる |
| 画面 | HDMI | デスクトップが出る |
| PCIeとVL805 | `pcie link up`、`pcie VL805 at ...`、`objtest: pcie` | リンクが上がり、VL805(1106:3483)が見つかり、プログラムの読込みの依頼が0を返す |
| USB | キーボードとマウスを挿す | xHCIが4ポートで上がり、入力が届く。割込みの数が増える(INTAが届く) |
| Ethernet | `genet: ... phy 1 up`、`objtest: ethernet` | PHYが見つかり、リンクが上がり、DHCPでアドレスを得て、SNTPで時計が合う |
| GPIO | `objtest: gpio banks`、`gpio events` | 3つのバンク、GPIO 26のプルの変化が通知される |
| LED | 目で見る | 起動中に2回と3回点滅する。400では最後に点いたままになる |
| 温度とクロック | `objtest: temperature and clocks` | 温度とARMのクロックが表示される |
| 乱数 | `objtest: random` | rng200から値が取れる |
| 再起動と停止 | システムの終了と再起動の操作 | 再起動でファームウェアから起動し直す。終了ではファームウェアが停止状態に入る |
| 命令セット | 全体の動作 | 未定義命令の例外が出ない(`tools/chk_isa.sh`で検査済み) |

## 3.7 Raspberry Pi 5のファン

Raspberry Pi 5のファンの端子のPWMの線は、RP1のGPIO 45からトランジスタを通して反転して駆動される。ピンがLowのときファンは全速で回り、ピンのプルダウンにより、駆動を始めるまでは全速である。ボードのデバイスツリーによれば、信号はRP1の2つめのPWMのブロック(`0x4009c000`)のチャネル3で、周期は41566ns、出力は反転である。デバイスツリーはCPUの温度に50℃、60℃、67.5℃、75℃の4つの段階を置き、ファンの強さを255のうち75、125、175、250としている。これはCPUを起動時のクロックのまま使う場合の値である。TessronOSはCPUを起動時より高いクロック(既定で1800MHz、設定によりファームウェアが許す最大の2.4GHzまで、3.9)で動かすので、カーネル(`kernel/sysdepend/rpi5/fan.c`)は早めに強く回す。段階は40℃、50℃、57.5℃、65℃で、それぞれ3℃下がると1つ前の段階に戻る。各段階でのファンの強さは255のうち120、170、220、255で、40℃未満では止まる。2026年10月5日の実機では、ブラウザで動画のページを開いたあとに全体が高温になり、デバイスツリーの値では足りなかった。ファンで足りないときは、ファームウェアが自分で下げる前にCPUのクロックを下げる。78℃で1800MHz、82℃で1500MHzにし、70℃を下回ると下げをやめる(`knl_cpu_heat`、タグ`0x00038002`)。下げも戻しもマシンの設定の上限(3.9)を超えない。戻すときは最大ではなく上限に戻り、上限が1800MHz以下なら78℃の段ではクロックは上限のまま変わらない。下げたときと戻したときは、コンソールに`fan: ... C, processors at ... MHz`と出る。ファンの制御のタスクは、ファームウェアが答える電源とクロックの状態(タグ`0x00030046`)も毎秒読み、変わるたびにコンソールに`power:`の行を出す。電源の電圧が下がった(`the voltage is low`、起動後にあれば`low voltage`)ことはここで分かる。

GPIO 45のどの機能がPWMの出力か、ブロックのクロックをどの源から作るかはRP1の公開資料の表にないので、起動時に調べる。チャネルを50%のデューティで動かし、クロックの源を水晶(50MHz)から順に、その下でピンの機能を1つずつ切り替えて、ピンを4000回読む。ピンが半分ほどHighで、何度も変化する組合せがPWMである。電源投入時のクロックは動いていない源を選んでいて、そのままではPWMの機能のピンがHighのまま変化しない(Raspberry Pi 5で確かめた)。同じ読取りからPWMの周波数が分かるので、ブロックのクロックが何であっても25kHzになるように範囲を決める。続けて75%のデューティでピンがHighである割合を読み、出力の反転の向きを確かめる。どの組合せでもPWMが見えないときは、クロックを元の設定に戻し、ピンを普通の出力として使い、最初の段階からは全速、それ未満では停止の入切だけで制御する。

温度は1秒ごとにファームウェアに問い合わせる(タグ`0x00030006`)。3回続けて読めなかったときは全速にする。制御のタスクは優先度6で、プログラムが負荷をかけていても動く。段階が変わるとコンソールに`fan: ... C, step ...`と出る。起動時には、PWMのクロックのレジスタの値、試した機能ごとの読取りの結果、決まった機能と範囲を出す。ファンの状態はシステムの実身のレコード9の`FAN_LEVEL`(255のうちの強さ)、`FAN_DRIVE`(`pwm`か`onoff`)、`FAN_SPEED`(デバイスツリーがファンの回転を読むとしているワードの値)で読める。CM5のIOボードのファンの端子は別の方式なので、制御はRaspberry Pi 5だけで行う。

## 3.8 ウォッチドッグ

BCM2712とBCM2711のPM(電源管理)のブロックはウォッチドッグを持つ。`PM_WDOG`(オフセット`0x24`)は16マイクロ秒の単位で20ビット、つまり17秒弱まで数え下ろし、0になると`PM_RSTC`(`0x1c`)のWRCFGの欄が全体リセットを指していればチップ全体をリセットする。書込みはどれも上位8ビットにパスワード`0x5a`を持たなければ無視される。ブロックのアドレスは、BCM2712では周辺機器の空間の`0x7d200000`(CPUからは`0x10_7d20_0000`)、BCM2711では`0x7e100000`(`0xfe100000`)で、ボードのデバイスツリーの`watchdog`のノード(`brcm,bcm2712-pm`、`brcm,bcm2835-pm`)と一致する。CM5はRaspberry Pi 5と同じBCM2712なので同じように使え、Raspberry Pi 4、400、CM4はRaspberry Pi 4系のイメージの再起動にもともと同じウォッチドッグを使っている。カーネルはこれを停止の検出(4.7.3)に使い、10秒後のリセットを設定し直し続ける(`kernel/sysdepend/rpi/wdog.c`)。止めるときは`PM_RSTC`に`0x102`を書く。QEMU virtにはウォッチドッグがない。

止まったときのリセットまでの時間は、マシンの設定の行`WATCHDOG`(秒、既定30、0で使わない、上限3600)で決まる。行がないとき、値が数でないときは既定の30秒にする。起動時に使い始めると、コンソールに`watchdog on`と出る。設定はシステム環境設定の〈機器〉の〈電源管理〉で、使うかどうかと秒数(5〜3600)を選んで〈設定〉を押すと書き込まれ、カーネルが設定の実身の変更を見てすぐに反映する(8.9)。今の値はシステムの実身のレコード9の`WATCHDOG_S`(秒、0は使わない)と`WATCHDOG_HW`(ボードのウォッチドッグを動かしているか)で読める。

CM5ではコンソールをRP1のUART0に移す(3.5.2)ので、RP1との間のPCIeが止まると、停止の報告もコンソールに出られない。その場合も、ウォッチドッグによるリセットは働く。

## 3.9 CPUのクロックの上限

CPUのクロックの上限は、マシンの設定の行`CPU_MHZ`(MHz)で決める。0はファームウェアが許す最大(Raspberry Pi 5とCM5では2.4GHz)である。行がないとき、値が数でないときは1800MHzとし、配布するシステムのボリュームの設定の実身(`etc/def`)の初期値も`CPU_MHZ 1800`にしてある。すでに配ったマシンで行がない場合も1800MHzになる。2.4GHzで4つのCPUを回すと消費電流が増え、電源が足りないと電圧が下がってボードが止まりやすいためである。2026年10月の実機で、起動時に2.4GHzへ上げるようにしてから落ちやすくなった。電圧が下がったかどうかはコンソールの`power:`の行(3.7)と、システムの実身のレコード9の`UNDERVOLT`で確かめられる。十分な電源を使う人は、システム環境設定で〈最大〉などに上げられる。

`config.txt`の`arm_freq`では上限を決めない。ファームウェアがその値を最大として扱い、OSから上げられなくなるからである。上限はカーネルがファームウェアに求めるクロックで決める(`kernel/sysdepend/cpu/core/armv8a/cpuclk.c`、`include/ts/cpuclk.h`)。

求めるクロックは、ファームウェアの最大(タグ`0x00030004`)、上限、ファンによる下げ(3.7)のうち最も低いもので、ファームウェアの最小(タグ`0x00030007`)を下回らない(`knl_cpu_want`)。上限がファームウェアの範囲の外にあれば、範囲の端に丸める。ファームウェアには、求めるクロックが変わったときだけ設定を求める(タグ`0x00038002`、`knl_cpu_hw_set`)。マシンの設定は、どの行を書き換えても全体を読み直すからである。

| 時点 | 処理 | 上限 |
| --- | --- | --- |
| ボードを見分けた直後(`knl_cpu_start`) | ファームウェアの範囲を問い合わせ、クロックを上げる | 1800MHz(設定はまだ読めない) |
| 実身マネージャの初期化の直後(`wm_cpu_conf_apply`) | 設定の実身の`CPU_MHZ`を読む | 設定の値 |
| デスクトップの起動時(`wm_dev_conf_apply`) | マシンの設定をすべて読む | 設定の値 |
| 設定の実身が書き換えられたとき | カーネルの見張りのタスクが読み直す(8.9) | 設定の値 |

起動時のクロックが1800MHzを超えることはないので、設定で上限を低くしてあるマシンでも、起動の途中で2.4GHzになることはない。上限が変わると、コンソールに`cpu: limit ... MHz; processors at ... MHz (the firmware allows ... to ...)`と出る。`processors at`はファームウェアが答えた設定後のクロックである。起動時は`TessronOS: processors at ... MHz (were ..., the firmware allows ... to ..., limit 1800 until the settings are read)`と出る。

今の状態はシステムの実身のレコード9で読める(8.4)。

| キー | 内容 |
| --- | --- |
| `ARM_HZ` | 今のクロック(ファームウェアの答え) |
| `ARM_LIMIT_MHZ` | 上限(0は最大) |
| `ARM_MIN_HZ`、`ARM_MAX_HZ` | ファームウェアが許す範囲 |
| `ARM_HEAT_HZ` | ファンによる下げ(0は下げていない) |
| `THROTTLED` | ファームウェアが答えた電源とクロックの状態(タグ`0x00030046`)。ビット0は今の低電圧、1は最大の引下げ、2はクロックの低下、3は温度のソフトリミット、ビット16〜19はそれぞれが起動後にあったこと |
| `UNDERVOLT` | `now`(今低電圧)、`before`(起動後に低電圧があった)、`no`、`unknown` |

Raspberry Pi 4、400、CM4でも同じ設定が効く。ファームウェアとのやり取り(`kernel/sysdepend/rpi/mbox.c`)は共通である。Raspberry Pi 4の最大はボードの版により1500MHzか1800MHzなので、既定の1800MHzは最大と同じになり、2000MHzや2200MHzを選んでも最大に丸められる。Raspberry Pi 4にはファンの制御がないので、ファンによる下げはない。CM5はRaspberry Pi 5と同じで、ファンの制御だけがない(3.7)。QEMUにはファームウェアがないので、上限を覚えるだけでクロックは変えない(`knl_cpu_hw_*`の弱い定義)。

カーネル内の試験`ktest_devob`の`test_cpu_limit`は、求めるクロックの計算をRaspberry Pi 5とRaspberry Pi 4の範囲で確かめ、マシンの設定に`CPU_MHZ`と`WATCHDOG`を書いて読み直させ、上限とウォッチドッグの秒数がカーネルとレコード9に出ること、値がないときと数でないときに既定に戻ることを確かめる。システム環境設定の操作は`tools/tests/ui_sysenv.steps`で試験する(11章)。
