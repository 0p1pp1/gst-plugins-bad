# gstreamer-0.10.xを用いたDVBアクセス, TSファイル再生用パッチ #

gstremer-0.10のgst-plugins-bad以下の各プラグインへ機能追加・修正をする.

## 変更箇所 ##

### gst-plugins-bad モジュール ###

<table border="1">
 <tr>
  <th> pluginライブラリ </th>  <th> エレメント名 </th>  <th> ソースディレクトリ </th>
 </tr>
 <tr>
  <td> libgstdvb.so </td>  <td> dvbsrc, dvbbasebin </td>  <td> sys/dvb/ </td>
 </tr>
 <tr>
  <td> libgstmpegtsdemux.so </td>  <td> tsdemux,tsparse </td>  <td> gst/mpegtsdemux/ </td>
 </tr>
 <tr>
  <td> libgstfaad.so </td>  <td> faad </td>  <td> ext/faad/ </td>
 </tr>
</table>


## パッチの内容 ##

### ver0.93での変更点 ###
  - 本家tsdemux.cのrewriteへの対応, mpegtsparse->tsparse, mpegtsdemux->tsdemux移行
  - gstreamer-0.10.23 へrebase
  - AAC音声のS/PDIF出力パッチ削除: 本家(gstreamer 1.2, pulseaudio 4.0)で対応済のため

### パッチ全体の主な内容 ###
 - EIT情報の取り出し・利用機能の日本向け修正
    (イベント追従の予約録画スクリプト dvb_sched_ev2/dvbevrec2で利用)

 - DVBモジュールのS2API対応 (チャンネル定義ファイルの形式)

 - mplayerと同様なURI `dvb://<adapter-No>@<channel-name>`形式のサポート

 - AAC音声再生機能の強化(デュアルモノ対応, チャンネル構成切り替わり対応)

 - video/audioストリームの選択・指示機能の追加
    デフォルトでメインのストリームを選択. AACデュアルモノのch指定や自動選択の追加

 - tsファイル再生時のシーク機能追加

 - その他再生時の不具合修正
    playbin2で`dvb://...` の再生が可能となるように

 - 外部ライブラリによるMULTI2復号機能 (要libdemulti2)
    DVBデバイスだけでなく,保存したTSファイルにも対応
    libdemulti2については配布しない。 インターフェースについてはdemulti2.hを参照


## ビルド方法 その1 ##
   システムにインストールされている既存の(-devel)パッケージを使用する。
   次に挙げる"Un-installed"の方法に比べ簡単であるが、
   システムにインストールされているgstreamer, gst-plugins-baseモジュールが
   0.10.36以上でないとビルドできない。

 1. ソース準備

        git clone [--depth 1] https://github.com/0p1pp1/gst-plugins-bad.git
        git checkout isdb/0.10.23

  あるいは以前にレポジトリをclone済みの場合は、以下でソースを更新

        git pull origin isdb/0.10.23

 2. 依存パッケージのインストール
    以下の各パッケージがインストールされている必要がある.(パッケージ名はFedoraの場合)

        gstreamer-devel, gstreamer-plugins-base-devel,
        gstreamer-plugins-bad-free-devel, gstreamer-python etc.;)

 3. configure再作成
    本パッチでは, 復号ライブラリの検出・設定のためconfigure.acを変更しているので,
    いきなり`./configure;make`ではNGで、 `./autogen.sh`の実行が必要.

        cd gst-plugins-bad; ./autogen.sh --prefix=/usr --libdir=/usr/lib64

  なおautogen.shを実行すると、生成したconfigureの実行まで自動的に行ってしまう。
  その場合、必要なプラグイン(のサブdir)であるgst/mpegtsdemux, sys/dvb, ext/faadを含めて
  ビルドするようデフォルト設定されている. (他のビルド可能なプラグインも一部含まれてしまうが)

  ./autogen.shに--noconfigureオプションをつければ、デフォルトの設定を使わない。
  autogen.sh実行後に通常どおり望みのオプションをつけて./configureすればよい.

 4. ビルドとインストール
  各サブdirの .libs/libxxx.soを~/.gstremaer-0.10/plugins/へコピーする

        pushd gst/mpegtsdemux
        make; cp .libs/libgstmpegtsdemux.so ~/.gstreamer-0.10/plugins/
        popd; pushd sys/dvb
        make; cp .libs/libgstdvb.so ~/.gstreamer-0.10/plugins/
        popd; pushd ext/faad
        make; cp .libs/libgstfaad.so ~/.gstreamer-0.10/plugins/


## ビルド方法 その2: 'un-installed'ビルド(参考) ##
  システムにインストールされているgstreamerパッケージを使用せず、
  すべて自前でソースからコンパイルし、インストールせずにそのまま使用する方法
  （gstreamer/scripts/gst-unintalled を使用する方法。 詳細は同ファイルを参照)

  システムにgstreamerがインストールされてなくても、バージョンが合わなく(古く)ても
  利用できる方法であるが、逆にシステムにインストールされている各プラグインは全く
  参照されないため、本パッチで変更されるプラグインだけでなく、アプリケーションから
  利用する可能性のあるプラグインはすべてビルドしなければならない。

 1. ソース準備
   - gstreamer本家から gst-plugins-bad以外の必要なモジュールを入手し、
        gstreamer,gst-plugins-badは >= 0.10.36をチェックアウト/ダウンロード。
        他のモジュールは0.10.xxならばおそらくOK。

   - パッチ済みのgst-plugins-bad (isdb/0.10.23ブランチ)を入手.
      上記ビルド方法その1の1.を参照

 2. ビルド準備
  gstreamer/scripts/gst-uninstalledにリンクしたスクリプト(例：gst-0.10)を起動し、
  各種環境変数がセットされたシェルを起動する

 3. ビルド
   コアライブラリなどの依存関係があるため、 gstreamer, gst-plugins-base を先にビルド
   あとは必要なモジュールをすべてビルドする。
   (できあがったライブラリをどこか別の場所にインストールする必要はない)

 4. アプリ実行
  上記2\.で起動したシェルで `gst-launch-0.10 ...` か、 あるいは、
  `gst-0.10 gst-launch-0.10 ...`


## 利用例 ##

### DVBテスト: ###
    gst-launch playbin2 uri=dvb://0@NHKBS1 (C-cでストップ)

  gstreamerからdvb:// のURIでアクセスするためには
  ~/.gstreamer-0.10/dvb-channels.conf に .mplayer/channels.conf[.s2] と同形式の
  チャンネル定義ファイルを作成しておく必要がある。 (mplayer用のファイルにlnしても可)

### 予約録画とか: ###
dvb_appsのスクリプト経由で.(詳しくはreadme-scripts.txtを参照 + 各コマンドの--help)

    dvb_sched_ev2 10:00 -c NHKBS1 -o ~/foo.ts


