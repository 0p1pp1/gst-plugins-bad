# gstreamer-1.xを用いたDVBアクセス, TSファイル再生用パッチ #

gstremer-1.2のgst-plugins-badへISDB向けの機能追加・修正をする.

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
 <tr></tr>
 <tr>
  <td> libgstmpegts-1.0.so </td>  <td> (mpegtsライブラリ) </td>  <td> gst-libs/gst/mpegts/ </td>
 </tr>
</table>


## パッチの内容 ##

### ver0.1の内容 ###

 - EIT情報の取り出し・利用機能の日本向け修正
    (イベント追従の予約録画スクリプト dvb_sched_ev2/dvbevrec2で利用)

 - DVBモジュールのS2API対応 (チャンネル定義ファイルの形式)

 - mplayerと同様なURI `dvb://<adapter-No>@<channel-name>`形式のサポート

 - AAC音声再生機能の強化(デュアルモノ対応, チャンネル構成切り替わり対応)
   なお、playbinなどでデュアルモノを再生するためには、
   gst-plugins-goodモジュールのaacparseにも対応が必要。
   (既に対応パッチは本家に投げられているよう模様)

 - video/audioストリームの選択・指示機能の追加
    デフォルトでメインのストリームを選択. AACデュアルモノのch指定や自動選択の追加

 - tsファイル再生時のシーク機能追加

 - その他再生時の不具合修正
    playbin2でdvb://.... の再生が可能となるように。

 - 外部ライブラリによるMULTI2復号機能 (要libdemulti2)
    DVBデバイスだけでなく,保存したTSファイルにも対応
    libdemulti2については配布しない。 インターフェースについてはdemulti2.hを参照


## ビルド方法 その1(未確認) ##
   システムにインストールされている既存の(-devel)パッケージを使用する。
   次に挙げる"Un-installed"の方法に比べ簡単であるが、
   システムにインストールされているgstreamer, gst-plugins-baseモジュールが
   バージョン1.2以上でないとビルドできない。

 1. ソース準備
    ブランチ1.2をチェックアウトする。

        git clone [--depth 1] https://github.com/0p1pp1/gst-plugins-bad.git
        git checkout isdb/1.2

  あるいは以前にレポジトリをclone済みの場合は、以下でソースを更新

        git pull origin isdb/1.2

 2. 依存パッケージのインストール
    以下の各パッケージがインストールされている必要がある.(パッケージ名はFedoraの場合)

        gstreamer1-devel, gstreamer1-plugins-base-devel,
        gstreamer1-plugins-bad-free-devel?, etc.;)

 3. configure再作成
    本パッチでは, 復号ライブラリの検出・設定のためconfigure.acを変更しているので,
    いきなり`./configure;make`ではNGで、 `./autogen.sh`の実行が必要.

        cd gst-plugins-bad; ./autogen.sh --prefix=/usr --libdir=/usr/lib64

    あるいは

        cd gst-plugins-bad; ./autogen.sh --noconfigure; ./configure .....

 4. ビルドとインストール

        pushd gst-libs/gst/mpegts/
        make
        sudo cp .libs/libgstmpegts-1.0.so.xx.xx /usr/local/lib64/; ldconfig
        sudo cp GstMpegts-1.0.typelib /usr/lib64/girepository-1.0/
        # FIXME ↑ この辺よくわかってない;)
        # /usr/local/lib64/girepository-1.0/ とか ~/.local/lib64/... とかに置けるのかも
        # あるいは どこか好きなとこに置いて、export GI_TYPELIB_PATH=foo/bar するとか
        popd; pushd gst/mpegtsdemux
        make; cp .libs/libgstmpegtsdemux.so ~/.local/share/gstreamer-1.0/plugins/
        popd; pushd sys/dvb
        make; cp .libs/libgstdvb.so ~/.local/share/gstreamer-1.0/plugins/
        popd; pushd ext/faad
        make; cp .libs/libgstfaad.so ~/.local/share/gstreamer-1.0/plugins/

  レジストリキャッシュ(~/.cache/gstreamer-1.0/registry.x86_64.bin)の削除が必要かも


## ビルド方法 その2: "un-installed"ビルド ##
  システムにインストールされているgstreamerパッケージを使用せず、
  すべて自前でソースからコンパイルし、インストールせずにそのまま使用する方法
  （gstreamer/scripts/gst-unintalled を使用する方法。 詳細は同ファイルを参照)

  システムにgstreamerがインストールされてなくても、バージョンが合わなく(古く)ても
  利用できる方法であるが、逆にシステムにインストールされている各プラグインは全く
  参照されないため、本パッチで変更されるプラグインだけでなく、アプリケーションから
  利用する可能性のあるプラグインはすべてビルドしなければならない。

 1. ソース準備
    - gstreamer本家から gst-plugins-bad以外の必要なモジュールを入手し、
        gstreamer,gst-plugins-badは >= 1.2をチェックアウト/ダウンロード。
        他のモジュールは1.2.xxならばおそらくOK。

    - パッチ済みのgst-plugins-bad (isdb/1.2ブランチ)を入手.
       上記ビルド方法その1の1.を参照

 2. ビルド準備
  gstreamer/scripts/gst-uninstalledにリンクしたスクリプト(例：gst-1.0)を実行し、
  各種環境変数がセットされたシェルを起動する

 3. ビルド
   ライブラリなどの依存関係があるため、 gstreamer, gst-plugins-base を先にビルド
   あとは必要なモジュールをすべてビルドする。
   (できあがったライブラリをどこか別の場所にインストールする必要はない)

 4. アプリ実行
  上記2.で起動したシェルで `gst-launch-1.0 ...` か、 あるいは、
  `gst-1.0 gst-launch-1.0 ...`


## 利用例 ##

### DVBテスト: ###
    gst-launch-1.0 playbin uri=dvb://0@NHKBS1 (C-cでストップ)  or
    totem dvb://0@NHKBS1

  gstreamerからdvb://のURIでアクセスするためには
  ~/.config/gstreamer-1.0/dvb-channels.conf に .mplayer/channels.conf[.s2] と同形式の
  チャンネル定義ファイルを作成しておく必要がある。 (mplayer用のファイルにlnしても可)

### 予約録画とか: ###
dvb_appsのスクリプト経由で.(詳しくはreadme-scripts.txtを参照 + 各コマンドの--help)

    dvb_sched_ev3 10:00 -c NHKBS1 -o ~/foo.ts


