# 第12章　開発と試験

開発環境の用意から、ビルド、Raspberry Pi 5への書き込み、WindowsのQEMUでの起動までの手順は、[ビルドと実行の手順](../build-and-run.md)に順を追ってまとめてある。この章は、その仕組みを説明する。

## 12.1 開発環境

ビルドはWSLのUbuntuで行う。ツールチェーンは`tools/setup_wsl.sh`が`~/.local/xPacks`以下にインストールする次のものである。

| ツール | バージョン |
| --- | --- |
| aarch64-none-elf-gcc(xPack) | 15.2.1-1.1 |
| qemu-system-aarch64(xPack) | 9.2.4-1 |
| Python | 3。イメージ作成ツールと試験の補助に使う |

`source tools/env.sh`でPATHが設定される。`TS_TOOLCHAIN`で別のツールチェーンを指定することもできる。

## 12.2 ビルド

makefileは`build_make/`にあり、再帰呼出しをしない1つのmakefileで全体をビルドする。対象マシンは`TARGET`で選ぶ。`TARGET`からアーキテクチャ`ARCH`(今はどれも`aarch64`)とコア依存の置き場の名前`ARCH_CORE`(`armv8a`)が決まり、ツールチェーン、命令セットの基準、インクルードパスとソースの一覧のコア依存の部分がそれで切り替わる(4.8)。出力先は`ARCH`によらず`TARGET`と`BUILD_ID`で決まる。

| コマンド | 生成物 |
| --- | --- |
| `make TARGET=_QEMU_VIRT_` | `tessronos.elf` |
| `make TARGET=_QEMU_VIRT_ qemu` | ビルドしてQEMUで起動する(Ctrl-A Xで終了) |
| `make TARGET=_RPI5_` | `tessronos.elf`、`tessronos.img`、`tessronos.map` |
| `make TARGET=_RPI5_ sdimg` | SDカードのイメージ`sd.img`(12.5) |
| `make TARGET=_RPI5_ sd SD=<FATパーティション>` | FATパーティションに`tessronos.img`と`config.txt`をコピーする |
| `make TARGET=_RPI4_` | Raspberry Pi 4、400、CM4の`tessronos.img`。ARMv8.0-Aで作る(3.6.2) |
| `make TARGET=_RPI4_ sdimg` | Raspberry Pi 4系のSDカードのイメージ`sd.img`(12.5、3.6.5) |
| `make TARGET=_RPI4_ qemu-rpi4`、`test-rpi4` | QEMUの`raspi4b`で起動する、カーネル内の試験を走らせる(3.6.6) |
| `make pkg PKG=<id>` | アプリケーションのパッケージ`obj/pkg/<id>.tpk`(11.8) |
| `make clean` | 生成物を削除する |

変数で構成を切り替える。

| 変数 | 機能 |
| --- | --- |
| `DESKTOP=1` | 試験ではなく、デスクトップが起動する構成でビルドする |
| `UITEST=1` | `DESKTOP=1`と併せて、UIの試験用の操作機能を組み込む(12.4) |
| `KTEST=1` | カーネル内の試験(12.3)を組み込む |
| `MOZC=1` | Mozcの変換エンジンと辞書をリンクする(先に`tools/mozc/build.sh`でビルドしておく) |
| `NETSTACK=netbsd`、`NETSTACK=lwip` | TCP/IPスタックを選ぶ。デフォルトは`netbsd`で、NetBSDのスタックのオブジェクトがなければ、リンクの前に`tools/netbsd/build.sh`を実行して作る(12.7)。`lwip`は以前のlwIPである(第9章) |
| `RUTRACE=1` | NetBSDのスタックで、ロックを5秒以上待っているタスクをコンソールに出力する(9.1.2) |
| `BROWSER=1`、`BROWSER_BLINK=1` | 別のリポジトリ`tessron-chromium`(既定はTessronOSと同じディレクトリ。`CHROMIUM=<場所>`で変える)からブラウザを取り込む。事前にChromiumのコンパイルが要る(14.3) |
| `KT_V8=<試験名>,…`、`KT_SITE=<URL>`、`KT_SITE_WAIT=<ミリ秒>`、`KT_SITE_CLICK=<x>,<y>,…` | 試験グループ`v8`のうち指定した試験だけを実行する、指定したサイトだけを開いてスクリーンショットを撮る、2枚目を撮るまでの時間を決める、その後に画面の座標をクリックする(14.10) |
| `WV_TEST=<ディレクトリ>` | `BROWSER=1`の試験のディスクの`/boot`に、そのディレクトリの`wv_Packages`と`wv.deb`(開発用のマシンでRaspberry Piのリポジトリから取ったインデックスとWidevineのパッケージ)を載せる。`test_blink_wv`が導入した状態を試すときだけ使い、リポジトリにもシステムのボリュームにも入れない(14.6.3.1) |
| `V3D=1`、`V3D=2`、`V3D=3` | Raspberry Pi 5のGPUを識別する、さらにMMUと割込みを設定して使えるようにし、起動の後で実機の試験とウインドウの合成の確認をまとめて行う、試験をせずに合成の確認だけをしてGPUで合成する(13.3.1、13.8.2、13.10) |
| `PCIE_POLL=1` | Raspberry Pi 4系で、PCIeの先のxHCIを割込みではなく一定の間隔で見る(3.6.4) |
| `OBJTEST=0` | Raspberry Pi 5と4系で、起動の後のデバイスの実身とボードの確認(`objtest:`)を行わない |
| `TS_VERSION=R1.110` | システムのバージョン。システムの実身の情報に`VERSION`として出る |
| `PKGS="a.tpk …"` | イメージに組み込むアプリケーションのパッケージ。既定は`etc/pkg/`の.tpkで、Raspberry Pi 4系では空(11.8) |
| `BUILD_ID=<名前>` | 生成物の出力先を`obj/<マシン><名前>/`と`tessronos<名前>.*`に分ける。同じソースツリーで複数の構成を並行してビルドするときに使う |

システムボリュームは、`etc/def`(システムの実身)、`etc/xtad`(サンプルと原紙)、ビルドで生成したプログラムの実身、組み込むパッケージの実身から`tools/mktsfs`で作成する。

実時間制御TAD(第15章)の実行系`lib/librtad/`と再生部`application/desktop/dtrt.c`、`dtrtwin.c`は、デスクトップと同じくカーネルに入る。音を鳴らす実時間メディア(`application/rtmedia/`)はnewlibとlibmを使うプロセスで、MP3の復号にminimp3(CC0、`application/rtmedia/minimp3.h`)を使う。ビルドでその実行ファイル`obj/rtmedia.elf`を作り、プログラムの実身(`01a0d8c4-52da-7c34-9d45-be6f708192a3`)のレコード1にする。映像は、別のリポジトリ`ffmpeg_btron`のMPEG Playerを使う。MPEG Playerを変えたときは`wsl.exe bash ffmpeg_btron/tessron/build-tessron.sh`でパッケージ`mpegplay.tpk`を作り直し、`etc/pkg/`に置く。

## 12.3 カーネル内の試験

試験はカーネルにリンクしてQEMU上で実行する(`tests/ktest/`)。試験はカーネルのタスクとして動き、1件ごとに`KTEST <名前> PASS`か`KTEST <名前> FAIL`をコンソールに出力する。最後に`KTEST SUMMARY <成功数>/<総数>`で集計を出す。

```sh
make TARGET=_QEMU_VIRT_ KTEST=1 test
make TARGET=_QEMU_VIRT_ KTEST=1 KTONLY=part,ob BUILD_ID=pt test
```

`KTONLY`で試験グループを名前で選ぶ(カンマ区切りで複数指定できる)。グループは`tests/ktest/ktest_<グループ>.c`のファイル単位で、`task`、`sync`、`time`、`mem`、`smp`、`board`、`sd`、`proc`、`fat`、`tsfs`、`ob`、`om`、`dp`、`fn`、`wm`、`part`、`mn`、`cab`、`tv`、`vt`、`usb`、`devob`、`snd`、`net`、`so`、`v8`、`gpu`、`cal`、`kana`、`slide`などがある。`cal`は予定表の実身の読み書き(11.10)、`kana`はかな入力練習の配列の表、問題文、判定、成績と、キーを離した通知(11.14)、`slide`はスライドの並びと画面への収め方(11.11)、`vols`は多くのUSBのディスクと区画の同時の取り付け、形式の見分け、ボリュームをまたぐ仮身とほかのボリュームからの参照の記録、抜き差しの繰り返しと4コアでの同時の使用、26台を超えるディスクと40区画のディスク(7.6、8.3)、`vt`は文章の縦書きの組版と原稿用紙(11.21)、`prn`は印刷のPDFの構造、字の位置、書体の部分集合、画像と各アプリのページ(11.22)、`scr`は画面印刷の取り込んだ画素と画面の一致、図形実身と保存先のディスク、トレーと図形への貼り込み、PNGの書き出し、PDF(11.24)、`dfix`はディスクの検査と修復、ツリーとビットマップが同時に壊れたときの作り直し、壊れた実身ブロックからの救出(7.5)、`rt`は実時間制御TADの実行系と二進TADへの変換(15.10)、`rump`は4つのCPUから同時に掛けたNetBSDのスタックへの負荷(9.1.2)、`smp`の`test_wait_result`、`test_del_ended`、`test_exd_stack`は別のCPUが解除した待ちの結果と、終了したタスクのスタックを返す時機(4.3)を試験する。`devob`は、デバイス管理に登録しないデバイスの実身(乱数、画面、入力、キーボードとポインタ、USBデバイス、システムのレコード3、4、9)の属性、読み書き、保護、仮身、接続と切断の通知を試験し、実身を通す経路と直接の関数の時間を出力する(8.10)。`snd`は、サウンドデバイスのチャネル、変換、混合、録音と、出力先の一覧と選択、再生中の切替え、選んだUSBオーディオの機能を抜いたときに自動の先へ移り挿し直すと戻ること(QEMUのモニタで2つ目の機能を挿す)、システムの実身のレコード8と機器の設定の`SOUND_OUT`による選択、ミキサーの混ぜ合わせ、プログラムごとと全体の左右の音量、消音、ソフトリミッタ、その設定の保存と読み戻しを試験する(6.5.1、6.5.6)。`fault`はデスクトップを起動し、Shift+Pauseによる強制終了と、例外で終了したプロセスのダイアログを試験する(5.5)。`fq`はデスクトップを起動し、デスクトップを止めた状態でのShift+Pause、解放のイベントが失われた後のShift+Pause、強制終了のウインドウからのウインドウのないプロセスの終了、システムコールの中で止まったプロセスの2段の終了、デスクトップがプロセスの依頼で出したパネル、デスクトップの立ち上げ直しを試験する(5.5.6)。`cred`は、デスクトップと同じ方法で起動したプログラムが利用者の資格で動き、時刻、ネットワーク、ボリューム、時計とネットワーク設定の実身を断られ、タイムゾーン、キー配列、ユーザ環境設定の項目、ユーザ情報と機器の設定とシステムの実身を許されること、一時的に与えた管理者の権限で時刻を変えられ、返したあとと期限が過ぎたあとは断られること、ログインした利用者の資格から管理者のグループが除かれること、管理者のパスワードの最初の設定と照合、信頼するプログラムの見分け、デスクトップのタスクが作る実身の所有者、USBの媒体の保護を試験する(8.7)。`board`は、メモリ上に組み立てたデバイスツリーとボードのリビジョンから、Raspberry Pi 5、CM5、CM5 Lite、Raspberry Pi 4、400、CM4を見分けること、起動したマシンのデバイスツリーからノードのプロパティと`ranges`、`dma-ranges`を読めることを試験する(3.5.4)。`sd`は`SD_EMMC=1`を付けるとeMMCに対して行う。`KTONLY`はコンパイル時の定義なので、`BUILD_ID`で出力先を分けないと反映されない。

試験環境は次の構成のQEMUである。

| 項目 | 値 |
| --- | --- |
| マシン | `virt,gic-version=2`、`cortex-a76`、4コア、2GB |
| ディスク | virtio-blkのGPTの試験用ディスク。試験が作り直すTSFSのパーティション、FAT32のブートパーティション、システムボリューム、FAT16とFAT12のパーティション |
| ネットワーク | virtio-net。`NET=user`でホストへ接続できる。このとき、ホストで試験用のFTPサーバ(`tools/ftpd.py`)とHTTPサーバ(`tools/httpd_test.py`、2180番がJSON、2181番がServer-Sent Events)が起動し、マイクロスクリプトの試験は仮身サンプルの通信用スクリプトをそのまま実行する |
| USB | xHCIに、キーボード、タブレット、マスストレージ2台、オーディオ |

`tools/qemu_run.sh`がQEMUを起動してコンソール出力を監視し、`tools/qemu_qmp.py`が試験からの要求に応じて、USBデバイスの抜き差しやキー入力をQEMUに送る。

Raspberry Pi 4系のイメージは、QEMUの`raspi4b`で`make TARGET=_RPI4_ KTEST=1 BUILD_ID=<名前> test-rpi4`として試験する(3.6.6)。試験の中の「QEMUではなくボード」を表す条件は`KT_BOARD`で、Raspberry Pi 5と4系で1になる。

電源断の試験は`make KTEST=1 KTONLY=tsfscut BUILD_ID=cut powercut`である。TSFSのボリュームを変更し続けるマシンを任意の時点で強制終了し、`fsck.tsfs`でエラーがないことを確認する。

## 12.4 デスクトップのUIの試験

UIの試験は`tools/ui_run.py`で行う。`make TARGET=_QEMU_VIRT_ DESKTOP=1 UITEST=1`でビルドしたカーネルは、2つ目のシリアルポートから1行ずつコマンドを受け取り、キーボードとポインタのイベントとして直接入力する(`application/desktop/dtuitest.c`)。ポインタは指定した座標へ一度に移動し、1行の処理が終わるごとにコンソールへ`@ut ok`と応答するので、固定時間の待ちは不要である。`ui_run.py`は最初の1回だけマシンを起動し、デスクトップが起動した時点のメモリとディスクの状態を一時ディレクトリの`ui_<tag>`に保存する。以後は、カーネルとディスクが同じである限りその状態から再開するので、1回の試験は起動を含めて数秒で終わる。

```text
click 110 100 1
wait 500
move 140 240
wait 150
move 230 321
click 230 321
win TADjsについての管理情報
shot info_window
```

| コマンド | 機能 |
| --- | --- |
| `move X Y`、`click X Y [B]`、`dclick X Y`、`hold X Y [B]`、`down [B]`、`up [B]`、`drag X1 Y1 X2 Y2 [B]` | ポインタとボタンの操作(ボタン0が左、1が右) |
| `key CODE [MODS]`、`text 文字列`、`wheel N` | キー入力(HIDのUsage IDと修飾キー、16進数)、英数字と記号の入力(記号は日本語キーボードの配列で打つ)、ホイール |
| `keydown CODE [MODS]`、`keyup CODE [MODS]` | キーを押したままにする、離す。同時に押すキーや、押したままのシフトを試験するのに使う |
| `open UUID` | 仮身をダブルクリックしたときと同じように実身を開く |
| `run PATH [N …]` | プログラムのファイルをプロセスとして起動する。数値(8個まで)が起動時の引数になり、プロセスIDを応答する。例外で終了するプロセスを起こして、例外のダイアログ(11.1)のスクリーンショットを撮るときなどに使う |
| `win 名前`、`wins` | 指定した名前のウインドウの外形、表示中のウインドウの一覧 |
| `wait MS`、`idle` | 待つ |
| `lag MS` | MSミリ秒のあいだ、ポインタを50ミリ秒ごとに左右へ動かし、画面のポインタがそこへ描かれるまでの時間を測る。最長の時間(ミリ秒)、動かした回数、200ミリ秒を超えた回数を応答する。プログラムの起動中などに、デスクトップが止まらないことを確かめるのに使う |
| `shot NAME`、`expect 文字列` | 画面をPNGで保存する、コンソールに指定の文字列が出るのを待つ(`ui_run.py`が処理する) |
| `absent 文字列` | その実行でコンソールに指定の文字列がまだ出ていないことを確かめる(`ui_run.py`が処理する)。スクリプトの最後に`absent !OACV!`を置き、権限の不足で断られた呼出しがないことを見る |
| `hmp コマンド` | QEMUのモニタにコマンドを送る(`ui_run.py`が処理する)。`$VOLS`は`--vols`のイメージのコピーの置き場になる。`drive_add`と`device_add usb-storage`でディスクを挿し、`device_del`で抜く |

メニューはサブメニューが開くまでに少し時間がかかるので、項目の上を通過するたびに`wait 150`程度を入れる。

`UITEST=1`でビルドしたカーネルは、プロセスの呼出しが権限の不足(`E_OACV`)で断られるたびに、コンソールへ`!OACV! pid=N "プログラム名" 操作 obj=UUID type=種類.副種類 "実身名" want=0x.. may=0x..`の1行を出す(`knl_ob_deny_note`、8.7.4)。`want`は求めた操作ビット、`may`は許されている操作ビットである。実身によらない呼出し(SVCゲートの権限の表、ボリュームの取り付けなど)では`obj`以下を省き、SVCゲートでは`want`が機能コード、`may`が表の権限になる。`UITEST=1`でないビルドも`make OACVLOG=1`で同じ行を出す。デスクトップから起動したプログラムは利用者の資格で動くので(8.7)、この行で、プログラムが要る以上の権限で実身を開いていないか、システムのものに書き込もうとしていないかを探す。`tools/tests/ui_user_apps.steps`は、プログラム箱のプログラム(パッケージで入るものを含む)を利用者の資格で一つずつ起動してウインドウが出ることを見て、システム環境設定での壁紙の変更と、バックアップの保存と復帰を行い、最後に断られた呼出しがないことを確かめる。

`--keep-disk ファイル`を付けると、実行を終えたディスクのイメージをそのファイルにコピーして残す。試験で作った実身を`tools/tsfs-export`で取り出して確かめるのに使う(11.22)。

`--cdrom イメージ`を付けると、そのイメージを入れたUSBのCD-ROMデバイス(`media=cdrom`のドライブを持つ`usb-storage`、ブロック2048バイト)をつないで起動する。イメージが変わると、保存した状態を使わずに起動し直す。BTRONのCD-ROMの試験(11.9、`tools/tests/ui_btcd.steps`)に使う。

WSL版のQEMUはユーザモードネットワークを持たないので、外部のネットワークを使う試験(`--net user`)は、WindowsのPythonからWindows版のQEMUで実行する。

`--vols ディレクトリ`を付けると、マシンに何もつないでいないUSBのハブを加え、そのディレクトリのディスクイメージ(`tools/tests/mkvoldisk.py`)を実行ごとに一時ディレクトリへコピーする。スクリプトは`hmp`でそれを挿して抜く。ディスク操作の試験(11.26、`tools/tests/ui_vols.steps`、`tools/tests/ui_disk_grow.steps`)に使う。`--keep-disk`と一緒に付けると、実行が書いたディスクイメージをそのディレクトリに書き戻すので、次の実行を`--disk`と`--fresh`で起動し直して、再起動をまたぐ振る舞いを試験できる(`tools/tests/ui_vols2.steps`)。

`--wav ファイル`を付けると、USBのサウンドデバイスを加え、その出力をQEMUがWAVEファイルに書く。音を出す試験(実時間制御TADの`tools/tests/ui_rt.steps`、`ui_rt_player.steps`など)で、鳴った音の大きさと周波数を後から確かめられる。

以前の`tools/desk_act.sh`(QEMUのモニタからマウスを少しずつ動かす方式)は1回に数分かかるので、新しい試験には使わない。

## 12.5 Raspberry Pi 5で動かす

`make TARGET=_RPI5_ sdimg`でSDカードのイメージを作る。パーティション1(FAT32)に`config.txt`、デバイスツリー、`tessronos.img`、プログラムを、パーティション2にシステムボリュームを置く。デバイスツリーはRaspberry Piのファームウェアの配布物から取得する。イメージはRaspberry Pi Imagerの「カスタムイメージを使う」などでSDカードに書き込む。

コンソールは3ピンのデバッグ用UARTに出力される。`RP1_CONSOLE=1`を指定するとコンソールを40ピンヘッダのUARTに移し、`RP1_UART=1`を指定するとヘッダのUARTを2つ目のシリアルポートとして使う。

Raspberry Pi 4、400、CM4では`make TARGET=_RPI4_ sdimg`でイメージを作る。パーティション1にファームウェア(`start4.elf`、`fixup4.dat`)、3つのデバイスツリー、`overlays/disable-bt.dtbo`も置く。コンソールは40ピンヘッダの8番と10番のUART0に出力される(3.6)。

## 12.6 ツール

| ツール | 機能 |
| --- | --- |
| `tools/mktsfs` | TSFSのボリュームを作成する |
| `tools/fsck.tsfs` | TSFSのボリュームを検査し、修復する |
| `tools/tsfs-import`、`tools/tsfs-export` | TADjs Desktop形式のファイル群からTSFSのボリュームを作成する、ボリュームの実身をファイル群に書き出す |
| `tools/mkdisk.py`、`tools/mksd.py`、`tools/mkfat.py` | 試験用ディスク、SDカードのイメージ、FATのボリュームを作成する |
| `tools/btbackup.py` | BTRONのBACKUP書庫を読み書きする |
| `tools/ftpd.py` | ネットワークの試験に使うFTPサーバ |
| `tools/ui_run.py` | UIの試験を、起動済みの状態から数秒で実行する(12.4) |
| `tools/mktpk.py` | アプリケーションのパッケージ(.tpk)を作成する。`--list`で内容を表示する |
| `tools/pkgstage.py` | イメージに組み込むパッケージを展開し、そのプログラムへの仮身を足したプログラム箱を作る(11.8) |
| `tools/netbsd/build.sh` | NetBSDのTCP/IPスタックのソースを取得し、rumpカーネルの部品として1つのオブジェクトにビルドする(12.7) |
| `tools/chk_isa.sh` | オブジェクトやイメージに、ARMv8.0-AにないLSEのアトミック命令や暗号拡張の命令が残っていないかを調べる(3.6.2) |
| `tools/mesa/probe.sh` | Mesaを取得し、その一部をプロセス用のツールチェーンでコンパイルしてみる。`pack`はV3Dのパケットを詰めるヘッダを生成してリポジトリに書く(13.6.4) |

## 12.7 NetBSDのスタックのビルド

TCP/IPスタック(第9章)は、NetBSD 11.0のカーネルのソースからビルドする。ソースはリポジトリに含めず、`tools/netbsd/build.sh`が次の順に処理する。

| 段階 | 処理 |
| --- | --- |
| `fetch` | NetBSDの配布物`syssrc.tgz`を取得してSHA512を確かめ、スタックのビルドに使うディレクトリだけを`~/netbsd/netbsd-11.0`(`NBSRC`で変える)に展開する。展開したソースは約27MB、2074ファイルである。取得先は通常の配布サーバ、保存用のサーバ、ftp.netbsd.orgの順に試し、途中で切れた転送は続きから取り直す。どこからも全体が取れないときは、展開するファイルだけを固めた`netbsd-11.0-rumpsrc.tgz`(`tools/netbsd/`にあるもの、無ければTessronOSのリリースの添付)を、その検査値を確かめて展開する(UPSTREAM.md) |
| `gen` | NetBSDのmakefileとconfig(1)が生成するもの(`vers.c`、ルートバスの`ioconf.c`、疑似デバイスの`ioconf.h`)と、`<machine/...>`のリンクを`~/tessronos-netbsd/gen`に作る |
| `compile` | rumpカーネルの基盤(librump)、ソケット層(librumpnet)、インタフェースと経路とIPv4、IPv6(librumpnet_net)と、TessronOSの部品(`peripheral_kernel/network/netbsd/rumpcomp/`)を、カーネルと同じツールチェーンで浮動小数点のレジスタを使わずにコンパイルする。言語の規格はNetBSD 11.0のカーネルと同じC11(`-std=gnu11`)である |
| `link` | rumpカーネルの規則どおり、NetBSDのシンボルに接頭辞`rumpns_`を付けてから1つのオブジェクトにまとめ、`rump_`で始まるエントリポイント以外をローカルにする。まとめるときに、NetBSDの`ip_savecontrol`の呼出しをTessronOSの部品の`__wrap_rumpns_ip_savecontrol`に向ける(`--wrap`。受け取ったIPv4のデータグラムのTOSを付随データに足す、9.2)。リンクセットの範囲もこのオブジェクトの中で決める。結果は`~/tessronos-netbsd/rumpnet.o`(`OUT`で変える)で、外から必要とするのはハイパーコール(`rumpuser_`、`rumpcomp_`)だけである |
| `notices` | コンパイラが読んだNetBSDのソースとヘッダ(依存関係のファイル`*.o.d`に載るもの)の著作権表示とライセンス条件を、重複を除いて`~/tessronos-netbsd/NOTICE-netbsd.txt`にまとめる(`THIRD_PARTY_NOTICES.md`) |

引数を付けなければすべての段階を実行する。ソースを取得するときだけネットワークを使う。命令セットの基準は`MARCH`で変える(既定は`armv8.2-a`)。Raspberry Pi 4系向けのビルドは`MARCH=armv8-a+crc OUT=~/tessronos-netbsd-v80`で作る(3.6.2)。コンパイルは12並列で10秒ほどで終わる。makefileは、このスクリプトか`rumpcomp/`のファイルが新しくなると、リンクの前にスクリプトを実行し直す。

コンパイルするファイルの一覧はスクリプトに書いてあり、NetBSDのmakefileの`SRCS`をコピーしたものである。NetBSDのバージョンを上げるときは、スクリプトの`NB_VER`と`NB_SHA512`を変え、新しいバージョンのmakefileと突き合わせて一覧を直す。展開するディレクトリの一覧(`EXTRACT_DIRS`)は、コンパイルで生成される依存関係のファイル(`*.o.d`)から求められる。展開したファイルを固め直したものを新しいバージョンのリリースに添付し、その検査値を`NB_SUB_SHA512`に書く。リンクと著作権表示の一覧には、一覧にあるソースのオブジェクトだけを使う。前のバージョンで作った置き場で作り直しても、新しいバージョンで無くなったソースのオブジェクトは取り込まない。
