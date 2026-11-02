# Karqri API Design Notes（日本語）

[English（正本）](api-design.md) | [日本語（翻訳版）](api-design-ja.md)

現在の契約は[API Reference](api-reference-ja.md)、CHARM-II、2026年の研究版、Karqriの関係は[歴史文書](history-ja.md)を参照してください。本書は既存の設計を説明するもので、APIの保証や検証結果を追加するものではありません。以下の実装・テストへの参照は、このソースツリー内のファイルを指します。

## 1. 設計の出発点と現在の範囲

Karqriは、CHARM-IIの復元と振る舞いの解析から発展した別のRTOSです。初回版の範囲は、同じカーネルをPOSIXとPicoで実行し、両方で6タスクのデモを動かすことです。ソース互換や旧OSの全サービスの再現は目的としていません。スーパーループ・協調実行・プリエンプティブ実行の体系的な比較や状態モデルの再設計は、今後の検討事項であり、現行APIの機能ではありません。

選んだ振る舞いを保存することは設計の方針であり、歴史的なアルゴリズムをすべて恒久的に維持する契約ではありません。Karqriは同順位ReadyのFIFO順を採用し、旧Memoryサービスやtask_eventアダプタを現行APIとして残していません。

Karqriは、Picoの実行基盤やデモの一部を含め、2026年の研究版で開発した具体的な実装と知見を継承し、自身の構成に合わせて変更しています。これは1986年のMC68000実装を直接移植することとは区別されます。振る舞いの継承、ソースの具体的表現、作者の申告、独立した由来の検証、権利の帰属は、それぞれ別に扱う必要があります。KarqriのMIT Licenseが歴史的な実装を再許諾するものではありません。本書はCHARM-II原典ソースを転載せず、ソースの独立性や権利上の問題の解消を証明するものでもありません。未解決の由来の問題を、本書によって解決済みとはしません。

通常のデモ動作と特殊経路の未検証事項は第13節で区別します。通常経路の成功は、すべての割込み競合を検証したことではなく、公開準備が全検証の完了を意味するわけでもありません。

## 2. 各層の責務と公開境界

| 層 | 担当すること | 担当しないこと | 主な根拠 |
| --- | --- | --- | --- |
| Queue | 呼出し側所有の要素をつなぐリスト、位置・優先度による挿入 | メッセージ内容、タスク待機、メモリ確保、排他 | [queue.h](../include/karqri/queue.h)、[queue.c](../kernel/queue.c) |
| Task | 開始関数と引数、優先度、状態ビット、遅延した戻り値 | Ready順の管理、CPU実行、スタック | [task.h](../include/karqri/task.h)、[task.c](../kernel/task.c) |
| Scheduler | Ready・Suspended所属、候補と論理current、優先度とFIFO、交代処理 | CPU状態の保存・復元、waiter取消し、コンテキスト解放 | [scheduler.h](../include/karqri/scheduler.h)、[scheduler.c](../kernel/scheduler.c) |
| Event | 通知ビット集合、ANY/ALL、waiter列、同期コールバック | タスク固有の管理、CPU実行の待機、タイムアウト | [event.h](../include/karqri/event.h)、[event.c](../kernel/event.c) |
| scheduler_event | Eventコールバックをタスクの遅延結果とScheduler待機解除へ接続 | スタック保存、実際のディスパッチ、待機途中の取消し | [scheduler_event.h](../include/karqri/scheduler_event.h)、[scheduler_event.c](../kernel/scheduler_event.c) |
| Timer | 呼出し側のプール、差分キュー、論理ティック、一度だけの満了通知 | ハードウェア時刻取得、経過時間補償、周期化、タスク固有処理 | [timer.h](../include/karqri/timer.h)、[timer.c](../kernel/timer.c) |
| timer_event | Timerの通知先と値をEventへ接続 | 独自キュー・状態、ディスパッチ、タイムアウトの巻戻し | [timer_event.h](../include/karqri/timer_event.h)、[timer_event.c](../kernel/timer_event.c) |
| ポート | スタックと実行コンテキスト、実行、受渡し、通知と切替の順序 | 全基本機能の汎用的な自動同期、任意の外部スレッド・コアとの接続 | [POSIXポート](../ports/posix/posix_port.c)、[Picoポート](../ports/pico/pico_port.c) |
| デモアダプタ | 環境ごとのティック・出力・待機・yield・再選択抑止との接続 | カーネル契約の変更 | [POSIXアダプタ](../demos/posix/track_demo.c)、[Picoアダプタ](../demos/pico/track_demo.c) |
| 共通処理 | 6タスクの移動、Event gate、共有領域の所有、描画と集計 | ハードウェアによるプリエンプションの実装 | [共通処理](../demos/common/track_demo.c) |

APIの境界では、Queue・Task・Event・Timerをアプリケーション向け、Scheduler・scheduler_event・timer_eventを内部処理や実行環境との統合向けに分類しています。`include/karqri/`に置かれているだけで、アプリケーションから内部状態を自由に操作できる保証にはなりません。ポートのヘッダーもアプリケーション用入口とアセンブリ補助関数を含むため、宣言の公開と直接利用の推奨は別です。

基本機能そのものには同期やディスパッチを埋め込まず、所有・寿命・直列化を契約として定めています。原子性の境界はキューのポインタ1回の更新ではありません。待機条件の判定から登録、Scheduler更新、コンテキスト受渡しまで、またはTimerのティック全体とコールバックが完了してからのディスパッチまでを一体で扱います。

## 3. Queue：汎用の要素操作に限定する

[queue.h](../include/karqri/queue.h)と[queue.c](../kernel/queue.c)は、番兵付きの双方向循環リストを実装します。呼出し側のオブジェクトへリンク情報を埋め込み、pop・remove後は切離し済みの状態に戻します。mallocや内容のコピーを行わない代わりに、リンク中の寿命と単一キュー所属は呼出し側が保証します。

優先度コールバックの戻り値は符号付きint32で、降順、同順位FIFOです。旧版の順序をすべて残すのではなく、KarqriではFIFOを採用しています。Schedulerはタスクのuint32優先度を直接比較する別の補助関数を持ち、符号付きの汎用コールバックへ強制変換しません。

同順位の登録順を保つには、キューが同じ規則で整列済みであり、リンク中に優先度が変わらないことが必要です。Queueが属性変更を自動検出する設計にはしていません。いったんremoveし、属性を変更して再挿入します。誤用を検査しないことは、任意の引数に動作が定義されるという意味ではありません。

`tests/test_queue.c`はFIFO・LIFO、同順位、負の優先度、コールバック引数、除去と再挿入を検査します。負の優先度のテストを、Schedulerの符号なし領域全体の保証へ読み替えないでください。

## 4. TaskとScheduler：属性・選択・CPU実行の分離

### 4.1 タスク状態は単一の列挙値ではない

初回版では歴史的なビットマスクを維持しています。EVENT_WAIT、RECEIVE_WAIT、SEND_WAIT、SUSPENDED、READY、RUNNING、JAM_WAITを定義していますが、すべてが実装済みサービスを意味するわけではありません。`karqri_task_is_waiting()`はEVENT・RECEIVE・SENDだけを検査します。

READY|SUSPENDEDや、選択されなくなったタスクに残るRUNNINGも現行の振る舞いです。初回版では状態を排他的な列挙値へ再設計せず、RUNNINGを実際のCPU実行の真値にもしていません。条件とスケジューリング状態の分離は今後の再検討候補であり、利用者が独自にビットの意味を変更してよいわけではありません。

Taskの停止・再開・優先度設定は、状態や属性だけを変えます。登録済みタスクは、キューとの整合性を保つScheduler・ポートの経路で制御します。遅延した戻り値の読取りは値を消費せず、clearは有効フラグだけを落とします。この区別が、Eventの即時結果と遅延結果を扱う基礎になります。

### 4.2 現行Readyの同順位順序はFIFO

旧CHARM-IIでは新たにReadyになったタスクを既存の同順位タスクの前へ挿入していましたが、Karqriではその群の末尾へ挿入します。VRTXとの歴史的な関係は、作者の記憶と残存資料に整合する説明であり、1986年の各判断を直接証明する記録ではありません。この解釈をソース由来の断定へ拡張しません。

現行`insert_ready_fifo`は優先度降順、同順位末尾です。生成、再Ready、起床、再開、優先度変更後の再登録が同じ規則に従います。同じ優先度を設定する場合は何も変えず、位置を維持します。時間片の交代ではcurrentまたは明示したタスクを同順位末尾へ移します。高優先度タスクのReady化で論理currentが変わっても、実際の切り替えはポートに依存します。

### 4.3 current・candidate・active・pending

| 値 | 意味 | 参照する層 |
| --- | --- | --- |
| Scheduler candidate | Ready列の先頭 | Scheduler |
| Scheduler current | 最後に論理選択したタスク。再選択抑止中はcandidateと異なり得る | Schedulerとポートの接続 |
| POSIX executing_context | 現在実行しているネイティブコンテキスト | POSIXポート |
| Pico active_context | 物理的な受渡し状態における実行コンテキスト | Picoポート・PendSV |
| Pico pending_context | 次回の受渡し先。NULLの空要求もあり得る | Picoポート・PendSV |

Schedulerの再選択は新currentの状態全体をRUNNINGへ置換しますが、旧タスクの状態を直しません。Ready列には選択中のタスクも含まれます。状態、Ready所属、論理current、CPU実行は同義ではありません。

### 4.4 再選択の抑止とタスク終了

再選択抑止はboolであり、キュー変更の停止やIRQ保護ではありません。基本の設定関数をfalseへ戻すだけでは新currentを選びません。Picoのラッパーは解除時に再選択と要求再評価まで行いますが、POSIXに同等のラッパーはなく、接続側が明示的に処理します。

待機や自己停止に入る場合は、別の実行可能コンテキストを残します。恒久的なアイドルタスクは必須とせず、全タスクの正常復帰を許しています。ただし、最後のReadyタスクが復帰する前にほかの待機・停止を解消する必要があります。Schedulerのremoveはwaiter取消しやコンテキスト破棄ではありません。[scheduler.h](../include/karqri/scheduler.h)も参照してください。

## 5. Event：ビット集合・スナップショット・コールバック

### 5.1 通知回数を数える仕組みではない

Eventは32ビットの通知をORします。同じビットの通知を繰り返しても回数は蓄積しません。ANY・ALLは要求マスクで判定しますが、結果はそのマスクとのANDではなく、**消費前のpending全体のスナップショット**です。成立時に消費するのは要求したビットだけです。

signalは、最初のpendingと通知をORした値を全waiterの判定に共通して使います。先行waiterの消費後も、後続の判定用スナップショットは変わりません。そのため、一つのsignalで複数waiterが完了することがあります。FIFOは走査順であり、不成立の先頭waiterが後続を妨げるものではありません。

この通知は排他的な所有権獲得を直接保証しません。周回デモの共通処理は所有者を別管理し、イベントで起床した後も所有確認と必要に応じた再待機を行います。Eventをセマフォやミューテックスと同一視しないでください。[demos/common/track_demo.c](../demos/common/track_demo.c)を参照してください。

### 5.2 基本のwaitとタスク実行の待機は別

Eventの待機条件が不成立なら、waiter情報設定、wait_started、FIFO登録の順に進みます。COMPLETEDではコールバックを呼ばず、BLOCKEDは登録済みを意味し、そのC関数の呼出し自体は戻ります。

scheduler_eventはコールバックでタスクの遅延結果を無効化し、EVENT_WAITへ移します。完了時には有効な遅延値を保存してからSchedulerでReady化します。即時成立では既存のタスク状態と遅延結果の有効性を維持します。CPU実行を待たせるのはポートです。待機途中の取消しや待機中タスク削除は採用していません。

この境界が、登録途中の割込みを危険にする理由でもあります。基本APIを呼ぶだけでは、判定から登録までの原子性は得られません。Picoラッパーが一連の処理を囲み、カーネルのアルゴリズムは変更していません。[pico_kernel.c](../ports/pico/pico_kernel.c)と[pico_switch.S](../ports/pico/pico_switch.S)を参照してください。

### 5.3 task_eventを残さない理由

旧task_eventは、Schedulerが存在しない段階でTaskとEventをつなぐための中間アダプタでした。TaskとEventだけを組み合わせる構成は、独立した正式な実行構成として維持していません。現行の接続は[scheduler_event](../kernel/scheduler_event.c)で、基本のEventテストとScheduler連携テストで振る舞いを確認します。現行scheduler_eventと旧task_eventは、二つの並列した公開APIの選択肢ではありません。

## 6. TimerとTimer/Eventアダプタ

Timerは、呼出し側の固定プール、差分キュー、論理ティック、一度だけの満了コールバックを提供します。ティックごとに先頭の差分を減算し、同時満了は登録順です。cancelは通知先ポインタとevents全値の最初の一致を除去し、後続の差分を補正します。満了関数は検索キーに含みません。

満了時はactiveから除去し、同期コールバックを実行してからfreeへ戻します。コールバック中に当該要素を再利用できない順序は、プール容量の評価にも影響します。同一キューへのstart・cancel・tickの再入は契約外です。周期コールバック、Timerハンドル、destroy、タスクのタイムアウトは提供していません。

delay=0は現行契約外で、1..UINT32_MAXを使います。過去のゼロ遅延のラップ試験は現行契約・テスト範囲に含まれず、ゼロの保証と解釈してはいけません。UINT32_MAXのテストは差分・補正と1ティック進行の境界検査であり、その回数の実時間経過を待った検証ではありません。

Timer本体はeventsを解釈しません。timer_eventだけがEventのビットマスクとして同期signalします。同ティック満了を合成しないため、後のsignalは先の消費後pendingを使います。cancelは将来通知1件を除くのみで、Eventのpendingやwaiterを巻き戻しません。プール枯渇時もEventへの失敗通知は送りません。

POSIXのTimerラッパーは全ティック処理とコールバックの完了後にディスパッチし、PicoはTimerと時間片処理をすべて終えてからPendSVを要求します。コールバック内で即座に切り替える設計への短縮は契約外です。

## 7. POSIXポート：協調実行と限定利用契約

[POSIXポート](../ports/posix/posix_port.c)は、[限定された実行契約](../ports/posix/README.md)のもとで、ucontext、ポート所有のmallocスタック、整数のトランポリンスロットを使います。ネイティブコンテキストはTaskと分離し、スロットから対応するタスクを特定します。

一つの実行環境に一つのカーネルを前提とします。現構造では`trampoline_slots[]`、コンテキスト表、host_context、実行中・実行状態の管理が必要です。すべてのグローバル状態の排除や複数インスタンス対応は採用していません。

初期化、読取り・書込み、実行、破棄はすべて単一の所有スレッドで行います。外部スレッド・シグナルハンドラ用API、非同期ティック・ディスパッチの橋渡し、スレッド所属の検査、実行中コンテキストの取消し・破棄は追加していません。初回版の対象外であり、呼べば動くと想定してよいものではありません。

タスクの正常復帰でSchedulerから除去します。destroyはスタックとスロットだけを解放し、ポートのポインタ表やcontext_countは消しません。固定されたタスク群の全終了と通知元・外部参照の終了を確認し、ホスト側でdestroyしてから旧ポートを退役させます。最後のタスクの復帰時に残存待機・停止を走査しないため、ホストへの復帰だけで正常終了とは判断できません。再選択抑止の違反も、Picoと同じ診断停止を保証しません。

限定契約は対応する利用方法の説明であり、追加の実行時検査を意味しません。将来の取消しやホストからの再開は、既存の不具合修正と混同せず、別の設計拡張として扱います。

## 8. Picoポート：要求・受渡し・RUNNINGの境界

### 8.1 保護境界と実装

| 境界 | 対策 | 実装 |
| --- | --- | --- |
| 割込み状態とフレームABI | PRIMASKの保存・復元、コンテキストとフレーム配置の表明 | [pico_interrupt.h](../ports/pico/pico_interrupt.h)、[pico_context.c](../ports/pico/pico_context.c) |
| PendSVでの受渡し | ハンドラ内部の割込みマスク、空pendingで無変更復帰、EXC_RETURN保持 | [pico_switch.S](../ports/pico/pico_switch.S) |
| 要求とEvent待機 | C側の共通要求規則、待機登録全体のマスク、PendSVで遅延R0を配送 | [pico_port.c](../ports/pico/pico_port.c)、[pico_kernel.c](../ports/pico/pico_kernel.c)、[pico_switch.S](../ports/pico/pico_switch.S) |
| 起動・終了とティック | 最初のbootstrap・最終復帰でRUNNINGを更新、アラーム所有とRUNNINGを分離、期限超過へ対処 | [pico_port.c](../ports/pico/pico_port.c)、[pico_tick.c](../ports/pico/pico_tick.c) |

競合は、待機判定から登録、共有カーネル操作、要求とactive・pendingの移行、起動・終了のティック境界にまたがります。一つのキュー操作の保護、started確認だけ、無条件の割込み有効化、配送確認のビジーループでは、全体への対策になりません。

### 8.2 切替要求と遅延R0

ここではDをコンテキストの`delayed_return_pending`、Pを`pending_context`、HをPendSV pending bitの略記として使います。

[ポートのC実装](../ports/pico/pico_port.c)の共通処理は、Scheduler currentからnextを取得し、pendingとSCB ICSRのSET・CLRを同じPRIMASK区間で更新します。別タスクへの古い要求は最新nextで置換し、不要な自己要求は取り消します。ただし、activeに完了値未配送のDがあれば自己PendSVを維持します。再選択抑止は未完了の受渡しを取り消さず、対応外なら状態変更前に診断停止します。

旧ラッパーの後続SETとC準備処理を分離する方式や、準備時に古いsaved_pspへR0を書く方式は現在使いません。PendSVは旧R4〜R11・PSPを保存し、old.saved_pspを更新してからnextの最新保存フレームへR0を配送します。自己切替も同じ順序です。配送後はDだけを解除し、Taskの遅延結果の有効フラグは維持します。

ラッパーは入口PRIMASK=0と特権Thread/PSPを検査し、C準備から要求発行までマスクします。復元後にISBと一度のD検査を行います。この検査は未完了待機が仮値0で戻る異常の検出であり、待機・再試行の手段ではありません。ISB単独で任意のpending例外が即座に受理されると一般化しません。適切な実行状態、優先度、要求維持が前提で、特殊タイミングの実機検証は別に残ります。

PendSVは受渡し完了後だけ元のPRIMASKを戻し、その後コンテキスト、PSP、タスクのレジスタ、カウンタを変更せず例外復帰します。C補助関数呼出しではEXC_RETURNとMSPの整列を維持します。これでMSP余裕の実測がすべて済んだことにはなりません。

### 8.3 ハードウェアアラームとRUNNING

アラームはstart前にも所有・稼働できますが、started=falseのコールバックはraw irq_countと再設定だけを行い、Timer・Scheduler・コンテキスト要求へ触れません。初回SVCでPSPを確立後、bootstrapが整合性を確認してstarted=trueを公開します。最初のカーネルティックは既存アラームの位相を受け入れます。最終正常復帰ではstarted=falseと要求取消しを公開してから最終SVCでmainへ戻ります。アラーム停止・所有権解放は別のホスト側操作です。

初回設定の期限超過は、前所有者のtarget_hiの問題を避けるためIRQを強制せず、後始末してfalseを返します。運用中の再設定の期限超過では、コールバック到達時のSDK条件を前提に、IRQを強制して次回を確保します。経過時間補償はしないため、カーネルティック数は正確な実時間換算ではありません。連続期限超過の既定上限8は有限の回復予算で、9回目に診断停止し、設定成功時にリセットします。Thread・PendSVが毎回進む保証や実測した遅延上限ではありません。

start・stopは同一コアの特権Thread専用で、登録・解除をPRIMASKで保護します。アラーム所有とタスク実行許可を一つのstartedで表す設計は採用していません。

## 9. 共通処理とポート固有の実行方式

移動、共有領域、Event gate、ANSI描画、集計を[demos/common](../demos/common/track_demo.c)で共有します。POSIXは六つの独立したucontextを協調実行し、実行中タスクのチェックポイントで論理ティックを進めます。PicoはハードウェアアラームとPendSVを維持します。

同じカーネル・共通処理という比較は、ティックの時間的同一性や、交代・移動・共有領域取得の回数の一致を要求しません。POSIXの通常・高速モードは待ち時間の違いであり、シグナル駆動のハードウェアプリエンプションではありません。アダプタの差を共通処理の差と混同しないでください。

[Picoデモアダプタ](../demos/pico/track_demo.c)のCDC接続最大10秒待ちと端末準備時間は、タスク・ティック開始前の出力準備です。カーネル起動APIやスケジューリング方針は変えず、既存のPASS条件を維持します。

[トレース](../ports/pico/pico_trace.c)はcooperative_three_task専用の内部計測で、[収集ツール](../tools/pico_validation/capture.py)と[Viewer](../tools/pico_validation/trace_viewer.py)がWindows側で取得・解析します。通常API処理でprintfせず、RAMへ記録して停止後にmainから出力します。トレースなしのデモに両ツールは不要です。HANDOFF_COMMITはタスク実行済みの証明ではありません。WASM・GUI・Viewer拡張を初回公開の追加条件にはしません。

## 10. 採用しなかった設計・延期した設計の索引

| 選択肢 | 現在の扱い | 実装または契約 |
| --- | --- | --- |
| 旧OSとのソース・バイナリ互換 | 初回版では要件にしない | [歴史文書](history-ja.md) |
| 同順位Readyの旧LIFOを恒久維持 | FIFOへ変更 | [scheduler.c](../kernel/scheduler.c) |
| TaskとEventだけの正式な第二構成 | 中間のtask_eventをScheduler連携へ置換 | [scheduler_event.h](../include/karqri/scheduler_event.h) |
| 標準Memoryサービス | Arena・partition APIを削除し、アプリケーション・環境側へ委任 | [現行APIの範囲](api-reference-ja.md) |
| インスタンス関連状態の全面排除・複数インスタンス対応 | 初期要件にせず、必要なポート固有状態を許容 | [POSIX契約](../ports/posix/README.md)、[pico_port.c](../ports/pico/pico_port.c) |
| タスク状態の全面再設計 | 歴史的な意味を維持し、将来再検討 | [task.h](../include/karqri/task.h) |
| Event待機途中の取消し・待機タスク削除 | 初回契約外、新APIなし | [scheduler.h](../include/karqri/scheduler.h)、[event.h](../include/karqri/event.h) |
| Timerハンドル・周期化・destroy・ゼロ遅延保証 | 追加せず、delay>=1 | [timer.h](../include/karqri/timer.h) |
| 全基本機能の内部lock・dispatch | 呼出し側・ポートの責務とし、外側操作の完了を維持 | [Event契約](../include/karqri/event.h)、[Timer契約](../include/karqri/timer.h) |
| POSIX外部スレッド・シグナル・実行中破棄 | 限定契約の対象外、一般的な診断保証なし | [POSIX契約](../ports/posix/README.md) |
| Picoの配送確認ビジーループ | PendSV配送と異常診断を使用 | [pico_switch.S](../ports/pico/pico_switch.S) |
| missed tickの経過時間補償 | 補償せず、有限の回復予算 | [pico_tick.c](../ports/pico/pico_tick.c) |

延期は将来実装の約束ではありません。この表は既存の設計上の選択を示し、新たな保証を提案するものではありません。

## 11. CHARM-IIとの概念的な共通点と相違点

優先度タスク、待機・停止ビット、Readyの交代、Event ANY/ALLとスナップショット消費、差分Timer、通知によるReady化が共通する振る舞いです。独特な歴史的状態を残す一方、同順位ReadyをFIFOへ変更するなど、明示的に選び直したものもあります。

Karqriは、呼出し側所有のオブジェクト、stdint型、コールバックによる汎用Event・Timer、Schedulerアダプタ、ポート分離を使います。POSIXはucontext、PicoはPSP・MSP・SVC・PendSVを使用し、旧68000のレジスタ・割込みフレームや旧システムコール番号を公開APIの互換要件にはしません。Memoryと旧通信サービス全体を現行機能として掲載しません。

これは概念・設計の比較です。ソース表現の由来、AI入力、権利帰属は別の証拠で評価する事項であり、構造の差だけで流用がないと証明されたとはいえません。[歴史文書](history-ja.md)で説明するように、2026年研究版からは知見に加えてPico・デモの具体的なコードも継承しています。これを歴史的なMC68000ソースと混同しないでください。

## 12. 実装・検証の索引

以下は実装とテスト定義の所在です。テストの存在は、全分岐・実行コンテキスト・割込みタイミングの検証済みを意味しません。通常実行の結果と補助検証は、特殊経路の実機確認と分けて扱います。

| 対象 | 実装 | テストまたは検証範囲 |
| --- | --- | --- |
| Queue | [queue.c](../kernel/queue.c) | [test_queue.c](../tests/test_queue.c) |
| Task | [task.c](../kernel/task.c) | [test_task.c](../tests/test_task.c) |
| Event | [event.c](../kernel/event.c)、[scheduler_event.c](../kernel/scheduler_event.c) | [test_event.c](../tests/test_event.c)、[test_scheduler.c](../tests/test_scheduler.c) |
| Timer | [timer.c](../kernel/timer.c) | [test_timer.c](../tests/test_timer.c) |
| Timer/Event | [timer_event.c](../kernel/timer_event.c) | [test_timer_event.c](../tests/test_timer_event.c)、[test_scheduler.c](../tests/test_scheduler.c) |
| Scheduler | [scheduler.c](../kernel/scheduler.c) | [test_scheduler.c](../tests/test_scheduler.c) |
| 層をまたぐ契約 | [公開・接続用ヘッダー](../include/karqri/scheduler_event.h)と第2〜6節 | [Makefile](../Makefile)にある七つのホストテストで範囲を分担 |
| POSIXコンテキストと接続 | [posix_port.c](../ports/posix/posix_port.c)、[posix_kernel.c](../ports/posix/posix_kernel.c) | [test_posix_kernel.c](../tests/test_posix_kernel.c) |
| Picoコンテキスト・受渡し・起動終了 | [pico_context.c](../ports/pico/pico_context.c)、[pico_port.c](../ports/pico/pico_port.c)、[pico_switch.S](../ports/pico/pico_switch.S)、[pico_tick.c](../ports/pico/pico_tick.c) | [validation_main.c](../ports/pico/validation_main.c)。通常実機結果は全特殊経路を網羅しない |
| 共通デモとCDC起動 | [共通処理](../demos/common/track_demo.c)、[POSIXアダプタ](../demos/posix/track_demo.c)、[Picoアダプタ](../demos/pico/track_demo.c) | デモの結果検査。[READMEの検証状況](../README-ja.md#検証状況)も参照 |
| 任意トレースとWindowsツール | [pico_trace.c](../ports/pico/pico_trace.c)、[capture.py](../tools/pico_validation/capture.py)、[trace_viewer.py](../tools/pico_validation/trace_viewer.py) | [test_capture.py](../tools/pico_validation/test_capture.py)、[test_trace_viewer.py](../tools/pico_validation/test_trace_viewer.py)。ツールのテストは実機タイミングの試験ではない |

## 13. 拡張に関わる制約と未確認事項

- 複数インスタンス、複数コア・所有スレッド、シグナルとの橋渡しを加える場合は、グローバルな検索表とCPUコンテキストの所属を再検討する必要があります。ミューテックスの追加だけでネイティブコンテキストを別スレッドへ移せません。
- 取消し・削除・タイムアウトには、Event waiter、Timer通知、Scheduler登録、ポートのフレーム、遅延結果の有効性の寿命を一体で扱う必要があります。現行のremove・freeだけで実装できるという契約はありません。
- ヘッダーの物理配置と公開・内部の論理分類は一致していません。場所だけでアプリケーション向け対応を判断できません。
- 原子性は同一コアでマスク対象となるIRQまでです。PRIMASKはNMI・HardFault・別コアとの同期を保証しません。
- スタックガードとsaved_psp範囲検査は、十分なスタック余裕の測定ではありません。MSPの最大使用量、IRQネスト、トレースの追加負荷は継続評価の対象です。
- 反復waiterの実コンテキストでの遅延値、同ティック起床後のFIFO実行、早期起床・自己PendSV・P/H置換と取消し・空要求、初回・最終SVC境界IRQ、アラームの期限超過・停止・再開には未検証部分が残ります。通常デモPASSからこれらの成功を推定しません。

既存のPOSIX通常・高速実行は6000論理ティックで完了しています。CDC修正版Picoの6000ティック・PASSは開発者の実機画面による確認報告であり、Codexによる独立した実機操作や書込み済みバイナリのハッシュ照合ではありません。既存の協調トレースは38件、8回の受渡し、ADGBEHCFI、overwritten=0ですが、ログにファームウェアのハッシュはなく、実行したバイナリは独立に同定されていません。通常実行の結果は特殊経路の全検証完了を意味しません。[READMEの検証状況](../README-ja.md#検証状況)と[API Reference](api-reference-ja.md)の第19節も参照してください。
