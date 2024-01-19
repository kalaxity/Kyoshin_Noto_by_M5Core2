# Kyoshin_Noto_by_M5Core2

<img src="./image/img.jpg" width="400px">

M5Stack core2で動作する、能登地方の強震モニタです。  
以下の記事のプログラムを改変して作成しました。  
http://www.ria-lab.com/archives/3339

また、防災科研（NIED）の強震モニタのデータを利用しています。  
http://www.kmoni.bosai.go.jp

## 使用時の注意
使用前には`main.cpp`に接続先のWiFi情報を記載する必要があります。  
その後、VSCode + PlatformIOでコンパイル&アップロードしてください。

## 未実装の機能
- 地震速報時音声の出力
  - core2では対応が難しく、バイブレーション機能で代替しています

## ライセンス
移植元では[NYSLライセンス](https://www.kmonos.net/nysl/)でしたが、知名度や利便性を考えMITライセンスで公開します。
