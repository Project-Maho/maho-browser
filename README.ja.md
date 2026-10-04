<div align="center">
  <img src="./docs/assets/product_logo.svg" width="80" alt="Maho">

  <h1>Maho</h1>

  <p><strong>あなたのために働くブラウザ。あなたを消耗させるためではなく。</strong></p>

  <p>
    <a href="https://mahobrowser.com">サイト</a> ·
    <a href="https://github.com/Project-Maho/maho-browser/issues">Issues</a> ·
    <a href="./CONTRIBUTING.md">貢献する</a> ·
    <a href="./SECURITY.md">セキュリティ</a>
  </p>

  <p>
    <img src="https://img.shields.io/badge/license-Sustainable%20Use-blue" alt="ライセンス: SUL">
    <img src="https://img.shields.io/badge/status-public-brightgreen" alt="ステータス: public">
    <img src="https://img.shields.io/badge/telemetry-none-success" alt="テレメトリ: なし">
  </p>

  <img src="./docs/assets/hero.gif" alt="Maho ブラウザ — 整理されたスペース、内蔵ブロック、ブラウザ内の AI とメール" width="90%">

  <p><a href="./README.md">English</a> · <a href="./README.zh-CN.md">中文</a> · <strong>日本語</strong></p>
</div>

---

ほとんどのブラウザは、あなたが画面を見続けるように設計されています。Maho は
逆に、できるだけ邪魔をしないように作られています。

終わりのない一つのウィンドウではなく、作業を**スペース**に整理し、広告や
トラッカーをデフォルトでブロックし、AI アシスタントとメールをブラウザに
組み込んで、アプリを行き来しなくて済むようにしました。

| | |
|---|---|
| **タブの氾濫ではなくスペース** | やっていること（仕事、調べもの、旅行など）ごとにタブをまとめ、その間を切り替える。一本の平たいタブバーに埋もれることはありません。 |
| **広告とトラッカーをデフォルトでオフ** | ブロックは内蔵。読みやすさと速さを両立させ、拡張機能を試行錯誤する必要はありません。 |
| **ページを読むアシスタント** | 見ている内容について質問したり、要約させたり、定型作業をスケジュール実行させたり。別アプリではなく、ブラウザの中で。 |
| **ブラウザの中のメール** | 受信トレイはブラウジングの隣に。メールを確認するために作業から離れる必要はありません。 |
| **データはあなたのもの** | テレメトリはありません。クライアントは外部に送信せず、利用状況がアプリから送られることもありません。自分の AI キーを持ち込んで、完全にコントロールすることもできます。 |

<div align="center">
  <img src="./docs/assets/command-bar.png" alt="Maho コマンドバー — 一か所で検索、タブ切替、アシスタントへの質問" width="85%">
  <p><em>検索、タブの切り替え、アシスタントへの質問を一つのコマンドバーで。</em></p>
</div>

---

## このソースコードについて

これは Maho クライアントの**ソース公開（source-available）**コードです。
Maho 独自の機能を実装する Chromium オーバーレイと、Rust クレート、WebUI、
ネイティブシェルで構成されています。

OSI 定義の**オープンソースではありません**。[Sustainable Use License](LICENSE)
により、自分の内部または個人用途での使用・変更、および無償・非商用での共有は
許可されます。ただし、商用での販売やホスティングは**許可されず**、Maho の
名称やロゴの権利も**付与されません**（[TRADEMARK](TRADEMARK.md)）。

- **含まれるもの：** Chromium オーバーレイ（`maho-chromium/`）、Rust
  クレート（`maho/crates/`）、WebView バンドル（`maho/web-ai/`）、メール
  バックエンド（`maho/mail-core/`）、ネイティブシェル（`maho/ios-shell/`、
  `maho/android-shell/`）。
- **含まれないもの：** ホスト型サービス（アカウント、同期、AI 課金プロキシ、
  更新フィード）と上流の Chromium —— 後者は自分で取得します。

## 自分でビルドする

Chromium のチェックアウト、Rust ツールチェーン、各プラットフォームの SDK が
必要です。

```bash
# 1) 上流の Chromium を取得（大容量）してオーバーレイを配置:
ln -s ../../maho-chromium chromium/src/maho

# 2) C++ ビルドがリンクする Rust コアをビルド
python3 maho-chromium/build/scripts/build_maho_core.py

# 3) ビルド
python3 maho-chromium/build/scripts/build_maho.py
```

ビルドはロックで直列化されます（一度に 1 つだけ。これは意図的です）。変更を
検証できる最小のターゲットだけをビルドしてください。実行するビルドには
**自分の** API キーと OAuth クライアントを使ってください。

## コントリビューション

Maho は小さなチームがメンテナンスしており、貢献を歓迎します。最初の pull
request の前に CLA に署名し、[コントリビューションガイド](CONTRIBUTING.md)を
読んでください —— `Signed-off-by` だけでは不十分です。セキュリティ上の問題は
公開 issue ではなく**非公開で**報告してください（[SECURITY.md](SECURITY.md) を参照）。

---

<div align="center">
  <sub>ソース公開であって、オープンソースではありません。製品については <a href="https://mahobrowser.com">mahobrowser.com</a> をご覧ください。</sub>
</div>
