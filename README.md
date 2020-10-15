# gstreamer-1.xを用いたDVBアクセス, TSファイル再生用パッチ #

gstremer-1.18のgst-plugins-badへISDB向けの機能追加・修正をする.

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

### 本ブランチ(isdb/1.18)の内容 ###
 - EPG情報を用いた、番組の受け継ぎ(リレー)視聴・録画への予備対応 \
  (NHK BS101->BS102でのスポーツ中継番組継続などで使われる) \
  dvb_appsの`{dvb_sched_ev3,dvbevrec3}`でリレー録画には対応したが、
  現状では再生時にリレーできずに終わってしまう。(録画自体は続けて出来ている)

 - isdb/1.16を本家1.18へrebase、本家1.18の小修正/デバッグ

### これまで(isdb/1.16...)の内容 ###

 - EIT情報の取り出し・利用機能の日本向け修正
    (イベント追従の予約録画スクリプト dvb_sched_ev3/dvbevrec3で利用)

 - DVBモジュールのS2API対応 (チャンネル定義ファイルの形式)

 - mpv/mplayerと同様なURI `dvb://<adapter-No>@<channel-name>`形式のサポート

 - AAC音声再生機能の強化(デュアルモノ対応, チャンネル構成切り替わり対応)
   なお、playbinなどでデュアルモノを再生するためには、
   gst-plugins-goodモジュールのaacparseにも対応が必要。

 - video/audioストリームの選択・指示機能の追加
    デフォルトでメインのストリームを選択. AACデュアルモノのch指定や自動選択の追加

 - tsファイル再生時のシーク機能追加

 - その他再生時の不具合修正
    playbin3でdvb://.... の再生が可能となるように。

 - 外部ライブラリによるMULTI2復号機能 (要libdemulti2)
    DVBデバイスだけでなく,保存したTSファイルにも対応
    libdemulti2については配布しない。 インターフェースについてはdemulti2.hを参照

## ビルド方法 その1: "un-installed"ビルド ##
  システムにインストールされているgstreamerパッケージを使用せず、
  すべて自前でソースからコンパイルし、インストールせずにそのまま使用する方法。
  ビルドシステムとして、gstreamerのgithubからgst-buildを持ってきて使用する。 (詳細はgst-build/README.mdを参照)

  システムにgstreamerがインストールされてなくても、バージョンが合わなく(古く)ても
  利用できる方法であるが、逆にシステムにインストールされている各プラグインは全く
  参照されないため、本パッチで変更されるプラグインだけでなく、アプリケーションから
  利用する可能性のあるプラグインはすべてビルドしなければならない。

 1. ソース準備
    - mkdir git; cd git
    - git clone https://github.com/0p1pp1/gst-plugins-bad
    - git clone https://github.com/gstreamer/gst-build
    - mkdir -p ~/gst/1.18
    - cd gst-build; ./gst-worktree.py add ~/gst/1.18 origin/1.18
    - ~/gst/1.18/subprojects/gst-plugins-bad.wrapを編集し、
      url(とpush-url), revisionをISDB版を指すように変更する。
```
[wrap-git]
directory=gst-plugins-bad
url=file:///...../git/gst-plugins-bad
push-url=file:///..../git/gst-plugins-bad
revision=isdb/1.18
```

 2. ビルド(例)
```
cd ~/gst/1.18; ./gst-env.py
mkdir build
meson -Dugly=disabled -Dges=disabled -Drstp_server=disabled -Dvaapi=enabled \
  -Dgst-examples=disabled -Dqt5=disabled -Dtests=disabled -Dexamples=disabled \
  -Ddoc=disabled -Dbuildtype=release -Dbackend=ninja builddir
ninja -C build
```
   (できあがったライブラリ等をどこか別の場所にインストールする必要はない。)

 4. アプリ実行
   `~/gst/1.18/gst-env.py gst-launch-1.0 ...`
    (gst-env.pyが各種環境変数を適切な値に設定する)


## ビルド方法 その2(未確認, *非推奨*) ##
   システムにインストールされている既存の(-devel)パッケージを使用し、
   修正のあるプラグインだけビルド・インストールする。
   上に挙げる"Un-installed"の方法に比べ高速・軽量であるが、
   システムにインストールされているgstreamerの各パッケージが
   バージョン1.18(以上?)でないとビルドできない。
   またプラグインやGObject型情報のレポジトリ, 依存ライブラリなどで依存関係や互換性が壊れ失敗する可能性が高い。

 1. ソース準備
    ブランチ1.18をチェックアウトする。

        git clone [--depth 1] https://github.com/0p1pp1/gst-plugins-bad.git
        git checkout isdb/1.18

  あるいは以前にレポジトリをclone済みの場合は、以下でソースを更新

        git pull origin isdb/1.18

 2. 依存パッケージのインストール
    以下の各パッケージがインストールされている必要がある.(パッケージ名はFedoraの場合)

        gstreamer1-devel, gstreamer1-plugins-base-devel,
        gstreamer1-plugins-bad-free-devel?, etc.;)

 3. 構成(コンフィグ)

        cd gst-plugins-bad; mkdir builddir;
        meson -Dauto_features=disabled -Dintrospection=enabled -Dfaad=enabled \
              -Ddvb=enabled builddir -Dmepgtsdemux=enabled builddir

 4. ビルドとインストール

        ninja -C builddir
        pushd builddir/gst-lib/gst/mpegts
        sudo cp libgstmpegts-1.0.so.xx.xx /usr/local/lib64/; sudo ldconfig
        sudo cp GstMpegts-1.0.typelib /usr/lib64/girepository-1.0/
        # FIXME ↑ この辺よくわかってない;)
        # /usr/local/lib64/girepository-1.0/ とか ~/.local/lib64/... とかに置けるのかも
        # あるいは どこか好きなとこに置いて、export GI_TYPELIB_PATH=foo/bar するとか
        pushd builddir/gst/mpegtsdemux
        cp libgstmpegtsdemux.so ~/.local/share/gstreamer-1.0/plugins/
        popd; pushd builddir/sys/dvb
        cp libgstdvb.so ~/.local/share/gstreamer-1.0/plugins/
        popd; pushd builddir/ext/faad
        cp libgstfaad.so ~/.local/share/gstreamer-1.0/plugins/

  レジストリキャッシュ(~/.cache/gstreamer-1.0/registry.x86_64.bin)の削除が必要かも

## 利用例 ##

### DVBテスト: ###
    gst-play-1.0 dvb://0@NHKBS1 (qでストップ)  or
    totem dvb://0@NHKBS1

  gstreamerからdvb://のURIでアクセスするためには
  ~/.config/gstreamer-1.0/dvb-channels.conf に .mplayer/channels.conf[.s2] と同形式の
  チャンネル定義ファイルを作成しておく必要がある。 (mplayer/mpv用のファイルにlnしても可)
  mplayer/mpvのチャンネル定義ファイルのフォーマットの例:
    NHK:DTV_DELIVERY_SYSTEM=8|DTV_FREQUENCY=515142857:33792
    NHKBS1:DTV_DELIVERTY_SYSTEM=9|DTV_FREQUENCY=1318000|DTV_STREAM_ID=0x40f1:101

### 予約録画とか: ###
dvb_appsのスクリプト経由で.(詳しくはreadme-scripts.txtを参照 + 各コマンドの--help)

    dvb_sched_ev3 10:00 -a 4 -c NHKBS1 -i -o ~/foo.ts


