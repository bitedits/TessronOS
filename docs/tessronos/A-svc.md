# 付録A　SVCの一覧

プロセスから呼べるシステムコールの一覧である。機能コードは(クラス << 16) | 番号で、`include/ts/svc.h`に定義がある。呼出し方法は5.6を参照。

## クラス0　μT-Kernel

| 番号 | システムコール |
| --- | --- |
| 1〜19 | `tk_cre_tsk`、`tk_del_tsk`、`tk_sta_tsk`、`tk_ext_tsk`、`tk_exd_tsk`、`tk_ter_tsk`、`tk_dis_dsp`、`tk_ena_dsp`、`tk_chg_pri`、`tk_rot_rdq`、`tk_rel_wai`、`tk_get_tid`、`tk_ref_tsk`、`tk_sus_tsk`、`tk_rsm_tsk`、`tk_frsm_tsk`、`tk_slp_tsk`、`tk_wup_tsk`、`tk_can_wup` |
| 20〜24 | `tk_cre_sem`、`tk_del_sem`、`tk_sig_sem`、`tk_wai_sem`、`tk_ref_sem` |
| 25〜30 | `tk_cre_flg`、`tk_del_flg`、`tk_set_flg`、`tk_clr_flg`、`tk_wai_flg`、`tk_ref_flg` |
| 31〜35 | `tk_cre_mbx`、`tk_del_mbx`、`tk_snd_mbx`、`tk_rcv_mbx`、`tk_ref_mbx` |
| 36〜40 | `tk_cre_mtx`、`tk_del_mtx`、`tk_loc_mtx`、`tk_unl_mtx`、`tk_ref_mtx` |
| 41〜45 | `tk_cre_mbf`、`tk_del_mbf`、`tk_snd_mbf`、`tk_rcv_mbf`、`tk_ref_mbf` |
| 46〜49 | `tk_dly_tsk`、`tk_get_tim`、`tk_set_tim`、`tk_get_otm` |
| 50〜59 | `tk_cre_cyc`、`tk_del_cyc`、`tk_sta_cyc`、`tk_stp_cyc`、`tk_ref_cyc`、`tk_cre_alm`、`tk_del_alm`、`tk_sta_alm`、`tk_stp_alm`、`tk_ref_alm` |
| 60〜74 | マイクロ秒単位の版。`tk_slp_tsk_u`、`tk_dly_tsk_u`、`tk_wai_sem_u`、`tk_wai_flg_u`、`tk_rcv_mbx_u`、`tk_loc_mtx_u`、`tk_snd_mbf_u`、`tk_rcv_mbf_u`、`tk_cre_cyc_u`、`tk_ref_cyc_u`、`tk_sta_alm_u`、`tk_ref_alm_u`、`tk_set_tim_u`、`tk_get_tim_u`、`tk_get_otm_u` |
| 75〜77 | `tk_set_utc`、`tk_get_utc`、`tk_get_prc` |
| 78 | `tk_chg_slt`(T-Kernel 2.0。タイムスライス、4.3.1) |

## クラス1　プロセス、メモリ、メッセージ

| 番号 | システムコール | 機能 |
| --- | --- | --- |
| 1 | `ts_get_mono` | 単調時刻 |
| 2 | `ts_cre_prc` | プロセスを生成する |
| 3 | `ts_ext_prc` | 自プロセスを終了する |
| 4 | `ts_ter_prc` | ほかのプロセスを強制終了する |
| 5 | `ts_wai_prc` | 子プロセスの終了を待つ |
| 6 | `ts_ref_prc` | プロセスの状態 |
| 7 | `ts_get_pid` | 自プロセスのID |
| 8 | `ts_gen_uuid` | UUIDを生成する |
| 9 | `ts_get_random` | 乱数。以前に作ったプログラムのために残し、呼んだプロセスの資格で乱数の実身を読む。ライブラリの`ts_get_random`は実身のキーを保持して直接読む |
| 10、11 | `ts_snd_msg`、`ts_rcv_msg` | プロセスメッセージ |
| 12 | `ts_get_arg` | 起動時の引数 |
| 13〜15 | `ts_map_mem`、`ts_unm_mem`、`ts_ctl_mem` | メモリをマッピングする、解除する、属性を変える |
| 16 | `ts_ref_cpu` | 動いているCPUの数と、各CPUがタスクを走らせた時間 |
| 17、18 | `ts_def_flt`、`ts_flt_ret` | プログラムが受け取るデータアボートのエントリを決める、例外の時点の状態(書き換えたもの)へ戻る(5.5.2) |

## クラス2　ファイル(FAT)

| 番号 | システムコール |
| --- | --- |
| 1〜10 | `fs_open`、`fs_close`、`fs_read`、`fs_write`、`fs_lseek`、`fs_stat`、`fs_fstat`、`fs_ftruncate`、`fs_truncate`、`fs_getdents` |
| 11〜16 | `fs_mkdir`、`fs_rmdir`、`fs_unlink`、`fs_rename`、`fs_statvfs`、`fs_sync` |
| 17〜20 | `fs_attach_dev`、`fs_detach_dev`、`fs_mounts`、`fs_utime` |

## クラス3　カレンダ

`dt_gettime`、`dt_settime`、`dt_gmtime`、`dt_localtime`、`dt_mktime`、`dt_getsystz`、`dt_setsystz`(1〜7)、`dt_getsyszone`、`dt_setsyszone`(8、9。タイムゾーンのIANAの名前、9.5)。

## クラス5　ソケット

| 番号 | システムコール |
| --- | --- |
| 1〜7 | `so_socket`、`so_close`、`so_bind`、`so_connect`、`so_listen`、`so_accept`、`so_shutdown` |
| 8〜11 | `so_send`、`so_recv`、`so_sendto`、`so_recvfrom` |
| 12〜17 | `so_getsockopt`、`so_setsockopt`、`so_getsockname`、`so_getpeername`、`so_poll`、`so_fcntl` |
| 18〜22 | `so_resolve`、`so_getifaddr`、`so_getdns`、`so_setifaddr`、`so_dhcp_start` |
| 23 | `so_getobj`(ソケットの実身のUUID) |
| 24 | `so_resolveall`(名前のIPv4とIPv6のアドレス、9.3.2) |
| 25 | `so_recvtos`(`so_recvfrom`に、データグラムが運んだTOSかトラフィッククラスを加えたもの、9.2) |

IPv6のアドレスの状態(`so_getifaddr6`、`so_getdns6`、`so_getip6`)と設定(`so_setip6`、`so_setdns6`)はSVCにしていない。プロセスは今の状態をシステムの実身のレコード7の`IPV6`、`ADDRESS6`、`GATEWAY6`、`DNS6`の行で読み、設定はネットワーク設定の実身の`$$IPV6`の行を書き換えて変える(9.3.4)。

## クラス6　コンソール

`tm_putstring`、`tm_putchar`、`tm_log_read`(1〜3)。`tm_log_read`は起動時からのコンソール出力を読み出す。

## クラス7　実身

| 番号 | システムコール |
| --- | --- |
| 1〜8 | `ob_opn_obj`、`ob_cls_obj`、`ob_cre_obj`、`ob_del_obj`、`ob_ref_obj`、`ob_lst_obj`、`ob_lnk_obj`、`ob_unl_obj` |
| 9〜14 | `ob_rea_rec`、`ob_wri_rec`、`ob_apd_rec`、`ob_trn_rec`、`ob_del_rec`、`ob_lst_rec` |
| 15〜23 | `ob_get_atr`、`ob_set_atr`、`ob_get_prt`、`ob_set_prt`、`ob_dup_key`、`ob_fnd_nam`、`ob_get_crd`、`ob_login`、`ob_set_pwd` |
| 24〜29 | `ob_att_vol`、`ob_det_vol`、`ob_rea_res`、`ob_wri_res`、`ob_del_res`、`ob_lst_res` |
| 30〜34 | `ob_beg_trx`、`ob_end_trx`、`ob_sch_rec`、`ob_trs_rec`、`ob_cpy_obj` |
| 35〜38 | `ob_ntf_evt`、`ob_can_evt`、`ob_map_rec`、`ob_unm_rec` |
| 39〜43 | `ob_get_dom`、`ob_set_dom`、`ob_get_ico`、`ob_set_ico`、`ob_ref_vol` |
| 44、45 | `ob_fnd_lnk`、`ob_lst_lnk`(箱のリンク先を名前で探す、一覧) |
| 46 | `ob_end_adm`(一時的に得た管理者の権限を返す、8.7) |
| 47 | `ob_wip_vol`(実身のあるボリュームのジャーナルを、置き終えてから0で書きつぶす。その実身のレコードを書ける者だけ、7.3) |

音の出力先の選択はSVCにしていない。プロセスは、出力先の一覧と選んだ出力先をサウンドデバイスの実身のレコード24と25(`OB_SND_OUTS`、`OB_SND_OUTSEL`)か、システムの実身のレコード8で読み、選ぶときはシステムの実身のレコード8に`SELECT`と出力先のIDを書く(6.5.1)。システムの音(ブザー、クリック音、〈試聴〉の音)を選んだ出力先で鳴らすときも、サウンドデバイスを自分で開かず、同じレコード8に`PLAY`と音の名前を書く。書込みの戻り値で鳴ったかどうかが分かる(6.5.5)。音量もSVCにしていない。ミキサーの全体の音量(左と右)と消音、音のデバイスを使っているプログラムの一覧とそれぞれの音量と消音は、サウンドデバイスの実身のレコード26(`OB_SND_MIX`、`SDMix`)で読み、レコード27(`OB_SND_MIXSET`、`SDMixSet`)に書いて変える(6.5.6)。

## クラス8　描画

`dp_fill_rect`、`dp_frame_rect`、`dp_line`、`dp_put_argb`、`dp_text`、`dp_text_width`、`dp_draw_tad`、`dp_img_decode`(1〜8)。自分のウインドウの描画環境にだけ描画できる(10.2)。

## クラス9　ウインドウマネージャ

| 番号 | システムコール | 機能 |
| --- | --- | --- |
| 1、2 | `wm_obj_gid`、`wm_obj_flush` | ウインドウの描画環境を取得する、描画内容を画面に反映する |
| 3 | `wm_key_char` | キーイベントを文字に変換する |
| 4〜9 | `mn_cre_men`、`mn_del_men`、`mn_chg_atr`、`mn_set_lst`、`mn_chg_idx`、`mn_pop_men` | メニュー |
| 10〜15 | `kc_open`、`kc_close`、`kc_hid_key`、`kc_choose`、`kc_list`、`kc_mode` | かな漢字変換 |
| 16 | `wm_msg_put` | メッセージ行 |
| 17、18 | `wm_look`、`wm_set_look` | 外観設定テーブルを読み出す、書き換える。ユーザ環境設定の項目はユーザ情報の実身に、ウインドウの見た目の項目は機器の設定の実身に書き込めるプロセスが、それ以外は管理者だけが書き換えられる。ユーザ環境設定とウインドウの見た目の項目は範囲を確かめ、ウインドウの見た目はすぐに画面へ反映する |
| 19 | `wm_draw_bar` | 自分のウインドウにスクロールバーを描く |
| 20〜29 | `tr_psh_dat`、`tr_add_dat`、`tr_set_dat`、`tr_get_sts`、`tr_ref_rec`、`tr_rea_rec`、`tr_sel_dat`、`tr_mov_dat`、`tr_del_dat`、`tr_clr_tra` | トレー(10.9)。カーネルのタスクが代わりに行い、バイト列はカーネルのメモリを通して受け渡す |
| 30、31 | `wm_obj_surf`、`wm_obj_put` | 作業領域の面をプロセスの共有メモリ領域にマッピングする、面に描いた矩形をウインドウに置いて画面に反映する(10.2) |
| 32 | `wm_key_put` | 入力の対象にならないウインドウ(`WM_ATTR_NOFOCUS`)から、入力の対象のウインドウへキーを入れる。そのウインドウが押された直後だけ受け付ける(11.15) |
| 33〜35 | `km_get`、`km_set`、`km_label` | キーの配列と付け替えを読む、設定する(機器の設定を書き込めるプロセス)、キーに書く文字を得る(11.15) |
| 36 | `wm_screen` | 画面の幅と高さ(ブラウザの`screen.width`と`screen.height`など) |

## クラス10　GPU

`gp_opn_gpu`、`gp_cls_gpu`、`gp_ref_gpu`、`gp_cre_bo`、`gp_del_bo`、`gp_map_bo`、`gp_sub_job`、`gp_wai_fen`、`gp_wai_bo`、`gp_shr_bo`、`gp_imp_bo`、`gp_ref_bo`(1〜12)。BOは自分のコンテキストの表の番号でだけ指定できる(13.5)。

## クラス11　シリアルポート

| 番号 | システムコール | 機能 |
| --- | --- | --- |
| 1 | `sr_lst_prt` | シリアルポート(種類が`TDK_SERIAL`のデバイス)の名前、説明、コンソールかどうか、速度の一覧 |
| 2 | `sr_opn_prt` | ポートを名前で開き、プロセスの中の番号を返す |
| 3 | `sr_cls_prt` | 閉じる |
| 4 | `sr_rea_prt` | 届いた分を読む。何も届いていなければ、指定の時間まで最初のバイトを待つ |
| 5 | `sr_wri_prt` | すべてのバイトを送ってから戻る |
| 6 | `sr_set_spd` | 速度(ビット毎秒)を設定する |

ポートはプロセスの番号でだけ指定できる。デバイスはカーネルが開いて持ち、プロセスが終わると閉じる(6.2)。

## クラス12　プログラムのUSBデバイス

| 番号 | システムコール | 機能 |
| --- | --- | --- |
| 1 | `us_lst_dev` | USBデバイスの一覧(`T_USBDEV`。VIDとPID、クラス、速度、ポート、役割、製品名) |
| 2 | `us_get_dsc` | デバイスの記述子を読む |
| 3 | `us_opn_dev` | クラスドライバが取らなかったデバイスを開き、プロセスの中の番号を返す |
| 4 | `us_cls_dev` | 閉じる |
| 5 | `us_ctl` | コントロールエンドポイントへの要求 |
| 6 | `us_set_alt` | インタフェースの代替設定を選ぶ |
| 7 | `us_clr_hlt` | エンドポイントの停止を解く |
| 8 | `us_xfer` | バルク、インタラプト、アイソクロナスのエンドポイントの転送 |
| 9 | `us_rst_dev` | デバイスをリセットし、アドレスと構成を元どおりにする |

開いたデバイスはプロセスの番号でだけ指定でき、プロセスが終わると閉じる(6.3.1)。

## 権限の表

システム全体に効くシステムコールは、次の権限を持つプロセスだけが呼べる。SVCゲート(`svc_gate.c`の`svc_rights`)が呼び出す前に表を引き、権限がなければ`E_OACV`を返す。表は`include/ts/svc.h`の機能コードの定義と並べて持ち、権限の種類は`TSN_RIGHT_*`、各機能コードの行の注記に要る権限を書いてある。管理者(システムのユーザ、または管理者のグループに属するユーザ)はすべての権限を持つ。システムプロセスのタスクは表を引かない。カーネルの中の各層が以前から行っている検査(`knl_dt_may_set`、`ob_att_vol`の管理者の検査など)はそのまま残し、カーネル自身の呼出しにも効く。

| 権限 | 持つもの | システムコール |
| --- | --- | --- |
| `TSN_RIGHT_CLOCK` | 時計の実身(デバイス)に書き込めるもの | `tk_set_tim`、`tk_set_tim_u`、`tk_set_utc`、`dt_settime` |
| `TSN_RIGHT_ZONE` | 暦の実身の属性を読み書きできるもの | `dt_setsystz`、`dt_setsyszone` |
| `TSN_RIGHT_CONF_DEV` | 機器の設定の実身に書き込めるもの | `km_set` |
| `TSN_RIGHT_CONF_USER` | ユーザ情報の実身に書き込めるもの | `wm_set_look`(項目によってはさらに機器の設定の実身への書込みか管理者であることを求める) |
| `TSN_RIGHT_ADMIN` | 管理者 | `so_setifaddr`、`so_dhcp_start`、`ob_att_vol`、`ob_det_vol`、`ob_set_dom` |

時計の実身の保護は所有者がシステム、グループが管理者、モードが`rw-r--r--`で変えられないので、時刻を変えられるのは実際には管理者だけである。暦、機器の設定、ユーザ情報の実身は、ACLで一般の利用者のグループ(利用者)に書込みを与えてあるので、デスクトップから起動したプログラムはタイムゾーン、キー配列、ユーザ環境設定の項目を変えられる(8.7)。

表にないシステムコールは、呼出し元自身のもの(自分のタスク、ファイル記述子、ソケット、ウインドウなど)か、キー、実身の保護、プロセスの実身が許すものにしか働かない。次のものはシステム全体に効くが、表ではなく実身の保護で守る。

| 操作 | 守るもの |
| --- | --- |
| ディスクの取り付けと取り外し(`fs_attach_dev`、`fs_detach_dev`) | ディスクのデバイスの実身のキーとその権限(8.6。システムが何も使っていないUSBの媒体は一般の利用者も読み書きできる) |
| 電源の切断と再起動 | システムの実身(デバイス)への書込み。所有者がシステム、グループが管理者、モードが`rw-rw-r--`で、ACLで一般の利用者のグループにも書込みを与える(8.6) |
| 音の出力先の選択 | システムの実身のレコード8への書込み(電源と同じ保護)。誰でも読み書きできるサウンドデバイスの実身からは選べない(6.5.1) |
| ネットワーク、機器、ユーザの設定 | 設定の実身の保護(8.9) |
| ほかのプロセスの終了、待ち、メッセージ | プロセスの実身の保護(`knl_obprc_may`、5.5) |

デスクトップから起動したプログラムは管理者でない資格で動く(8.7)。表の権限を持たない呼出しは`E_OACV`で断られ、プログラムはデスクトップに管理者のパスワードを尋ねさせて(`DT_RQ_ADMIN`)、その操作の間だけ管理者の権限を得る。時刻を変えるユーザ環境設定の小物(`application/userenv`)、ネットワーク設定の小物(`application/netenv`)、ネットワークを開始するファイル変換(`application/xfconv`)がこの手順をとる。カーネルのタスクとして動くネットワークの時刻合わせは、システムプロセスのタスクなので表を引かない。キー配列の小物(`application/keymap`)、システム環境設定の小物(`application/sysenv`)の配色、壁紙、画面、キーボードの設定は、一般の利用者が書ける実身への書込みなので尋ねない。
