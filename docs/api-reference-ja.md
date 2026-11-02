# Karqri API Reference（日本語）

[English（正本）](api-reference.md) | [日本語（翻訳版）](api-reference-ja.md)

このリファレンスは、現在の公開ヘッダー、実装、テスト定義に基づいてAPIを説明します。英語版が正本、日本語版は翻訳版です。各節のソースとテストへの参照で仕様を確認できます。宣言やテストの存在と、実行による検証結果は区別し、既存の実行記録の範囲と限界は第19節にまとめます。本書の整備によって新たな検証結果を追加するものではありません。

## 1. 範囲・読取り方

`include/karqri/`の全7ヘッダーを収録する。APIの境界ではQueue/Task/Event/Timerをアプリケーション向けの基本機能、Scheduler/scheduler_event/timer_eventを内部処理や実行環境との統合向けに分類する。配置ディレクトリだけでapplication APIとは判断しない。POSIX/Picoのportヘッダーも別層として収録する。内部assembly helper・診断・traceは公開利用APIへ昇格させず付録で区別する。削除済みMemory/task_event、message送受信、sleep、timeout、Task強制delete、周期Timerは現行サービスではない。

各節のC宣言は現行headerのprototype。表は各関数の引数用途・戻り値・副作用・固有制約を示す。共通条件は各節と次節を併せて適用する。宣言があること、testから参照されること、実行記録があること、全入力/割込みタイミングが検証されたことは別である。

収録数：Kernel/integration 48関数、POSIX/Pico port 32関数の計80関数。加えて内部inline/診断/条件付きtrace 8関数を付録に収録する（計88）。関数ポインタ型・型定義・主要定数・公開externも併記する。demo-private headerやPythonツールはKernel APIの対象外。

## 2. 共通の実行・寿命・エラー契約

- Kernel primitiveに内部lock、割込み禁止、allocation、CPU dispatchはない。TaskでもISRでも利用可能かはcallerが全競合操作（read、queue走査、callbackを含む）を直列化できるかによる。任意ISR安全の一括保証はない。POSIXは下記のowner-thread制限をさらに適用する。
- objectはcaller所有で、参照・リンク・callback中は有効で安定したアドレスが必要。初期化は初回使用前。登録中の移動/個別再初期化、同一nodeの二重所属は禁止。NULLは明示許可した場合のみ。誤用は一般に検査せずerror codeを返す保証もない。
- callbackは呼出し元で同期実行する。同じEvent/Timerの走査を無効にする直接/間接再入は禁止。Scheduler論理選択の変更は許容されても、外側のEvent signal/Timer tick終了前のCPU dispatchは不可。
- 実行Taskのblock/self-suspendには別の実行可能contextが必要。必要ならapplicationがdriver/idleを置く。全Taskは正常returnでき、恒久idleは必須ではない。waitの途中cancel、wait中Taskの除去/破棄は契約外。
- suppressionはboolでありnest count/IRQ lockではない。以前の値を保存して戻す。suppression中のblock/self-suspend/Task returnは不可。raw setter解除だけではrescheduleしない。
- POSIXは単一owner OS thread、固定context群の正常終了後destroy。外部thread/signal handler、並行instance、未終了群のhost再開は非対応。違反が必ず診断される保証なし。
- Picoは同一core・単一runtime・privileged実行が基本。Task側でraw Timer/Event/Queue/Task/Schedulerを直接操作する場合、tick IRQとの競合保護はcaller責務。PRIMASKは保存値へ復元し、mask中にSVC/PendSVを待たない。NMI/HardFault/他coreはこの保護の対象外。

## 3. 型・定数・状態モデル

| 層 | 型/field | 意味 |
| --- | --- | --- |
| Queue | `KarqriQueueNode { next, previous }` | intrusive双方向循環list。sentinelとelementは同型。`KarqriQueuePriority`は`int32_t (*)(const KarqriQueueNode *, void *)` |
| Task | `KarqriTaskEntry = void (*)(void *)`、`KarqriTaskState = uint32_t` | entry/argument、uint32 priority、node/state、uintptr遅延値＋bool valid。stack/native contextは含めない |
| Event | ANY/ALL、COMPLETED/BLOCKED、`KarqriEventWaitResult { disposition, events }` | 32 pending bits。countではない。ANYは交差非0、ALLは要求全bit成立 |
| Event callback | `KarqriEventWaitOperations` | `void wait_started(void*)`、`void return_value_set(void*,uint32_t)`、`void wait_completed(void*)`。ownerを渡す |
| Event storage | `KarqriEvent { pending_events, waiters }`、`KarqriEventWaiter` | waiterはnode/owner/operations/requested_events/modeを保持。callerが確保 |
| Scheduler | `KarqriScheduler` | Ready/Suspended sentinel、logical current、suppression。Readyは選択中Taskも含む、uint32 priority降順・同順位FIFO |
| adapter | `KarqriScheduledEventWaiter` | raw EventWaiter＋Scheduler/Task参照。完了時に遅延値→readyへ接続 |
| Timer | `KarqriTimerExpiry = void (*)(void *, uint32_t)`、`KarqriTimer` | node/destination/expiry/events/remaining_ticks。eventsはTimerではopaque値、remaining_ticksはdelta |
| Timer pool | `KarqriTimerQueue { free_timers, active_timers }` | caller poolを両queueで管理。free entryもリンク中 |

Task bitsはEVENT_WAIT=1、RECEIVE_WAIT=2、SEND_WAIT=4、SUSPENDED=8、READY=16、RUNNING=32、JAM_WAIT=64。WAIT_STATESは最初の3bitのみ。RECEIVE/SEND/JAM定数は対応サービスの実装宣言ではない。READY|SUSPENDEDや旧Taskに残るRUNNINGは意図したhistorical semantics。stateだけでCPU実行やqueue所属を推定しない。Queue汎用priorityは**signed int32**、Task/Schedulerは**unsigned uint32**で異なる。


## 4. Queue

宣言：[include/karqri/queue.h](../include/karqri/queue.h)。実装：[kernel/queue.c](../kernel/queue.c)。検証定義：[tests/test_queue.c](../tests/test_queue.c)。

Task/直列化済みISRの非blocking primitive。callback評価を含む操作全体を保護する。誤用検査なし。

```c
void karqri_queue_init(KarqriQueueNode *node);

bool karqri_queue_is_empty(const KarqriQueueNode *queue);

void karqri_queue_insert_before(
    KarqriQueueNode *position,
    KarqriQueueNode *node);

void karqri_queue_push_front(
    KarqriQueueNode *queue,
    KarqriQueueNode *node);

void karqri_queue_push_back(
    KarqriQueueNode *queue,
    KarqriQueueNode *node);

KarqriQueueNode *karqri_queue_pop_front(KarqriQueueNode *queue);

void karqri_queue_remove(KarqriQueueNode *node);

void karqri_queue_insert_priority(
    KarqriQueueNode *queue,
    KarqriQueueNode *node,
    KarqriQueuePriority priority_of,
    void *context);
```

| API | 動作・引数・戻り値・固有制約 |
| --- | --- |
| `karqri_queue_init` | nodeを自己循環のdetached状態へ初期化する。sentinelにも使用。リンク中の個別初期化は禁止。戻り値なし。 |
| `karqri_queue_is_empty` | queue sentinelのnextが自身ならtrue。読取りも共有アクセスの直列化が必要。 |
| `karqri_queue_insert_before` | 所属queue内のposition（sentinel可）の直前へdetached nodeを挿入。戻り値なし。 |
| `karqri_queue_push_front` | queue先頭へdetached nodeを挿入。pop_frontとの組合せはLIFO。 |
| `karqri_queue_push_back` | queue末尾へdetached nodeを挿入。pop_frontとの組合せはFIFO。 |
| `karqri_queue_pop_front` | 先頭を外して返す。空ならNULL。返すnodeはdetached。 |
| `karqri_queue_remove` | elementを外してdetachedに戻す。初期化済みdetachedへの実行も可。sentinelのremoveは禁止。 |
| `karqri_queue_insert_priority` | priority_of(node, context)のint32_t降順へ挿入。同順位FIFO。contextは無変換（callbackが許せばNULL）。整列済みqueueと安定したpriorityが前提。callback中のqueue変更は禁止。 |

**宣言と検証の区別**：上記は全て実装あり。次は当該test/validation source内の直接参照の有無（実行成功や全分岐coverageの意味ではない）。

直接参照あり：`karqri_queue_init`、`karqri_queue_is_empty`、`karqri_queue_push_front`、`karqri_queue_push_back`、`karqri_queue_pop_front`、`karqri_queue_remove`、`karqri_queue_insert_priority`。

このtest内の直接参照なし：`karqri_queue_insert_before`。内部呼出し・他test/demoでの利用とは区別し、単独試験済みとはしない。

## 5. Task

宣言：[include/karqri/task.h](../include/karqri/task.h)。実装：[kernel/task.c](../kernel/task.c)。検証定義：[tests/test_task.c](../tests/test_task.c)。

論理属性の非blocking read/write。Scheduler登録済みTaskをraw setterで不整合にしない。状態bit検査も共有read。

```c
void karqri_task_init(
    KarqriTask *task,
    KarqriTaskEntry entry,
    void *entry_argument,
    uint32_t priority);

KarqriTaskState karqri_task_state(const KarqriTask *task);

void karqri_task_state_replace(KarqriTask *task, KarqriTaskState state);

void karqri_task_state_set(KarqriTask *task, KarqriTaskState state_bits);

void karqri_task_state_clear(KarqriTask *task, KarqriTaskState state_bits);

bool karqri_task_state_has_any(
    const KarqriTask *task,
    KarqriTaskState state_bits);

bool karqri_task_state_has_all(
    const KarqriTask *task,
    KarqriTaskState state_bits);

bool karqri_task_is_waiting(const KarqriTask *task);

bool karqri_task_suspend(KarqriTask *task);

bool karqri_task_resume(KarqriTask *task);

uint32_t karqri_task_priority(const KarqriTask *task);

uint32_t karqri_task_set_priority(KarqriTask *task, uint32_t priority);

void karqri_task_set_delayed_return(
    KarqriTask *task,
    uintptr_t value);

bool karqri_task_has_delayed_return(const KarqriTask *task);

uintptr_t karqri_task_delayed_return(const KarqriTask *task);

void karqri_task_clear_delayed_return(KarqriTask *task);
```

| API | 動作・引数・戻り値・固有制約 |
| --- | --- |
| `karqri_task_init` | entry/entry_argument/uint32_t priorityを保存し、node detached、state=READY、遅延値0・invalidへ初期化。Scheduler登録・stack作成・起動はしない。entryと引数を実行まで有効に保つ。 |
| `karqri_task_state` | 現在のstate bitmaskを返す。実行中Taskの証明にはならない。 |
| `karqri_task_state_replace` | state全体を指定値へ置換。queue membershipは変えない。 |
| `karqri_task_state_set` | state_bitsをORする。queue membershipは変えない。 |
| `karqri_task_state_clear` | state_bitsをAND NOTで解除。queue membershipは変えない。 |
| `karqri_task_state_has_any` | 指定bitの少なくとも1つが立てばtrue。mask=0ではfalse。 |
| `karqri_task_state_has_all` | 指定bitが全て立てばtrue。mask=0ではtrue（実装式に基づく）。 |
| `karqri_task_is_waiting` | WAIT_STATES（EVENT/RECEIVE/SEND）のいずれかが立てばtrue。SUSPENDED/JAMはこの判定に含まない。 |
| `karqri_task_suspend` | 既にSUSPENDEDならfalse。それ以外はbit設定し、wait中でなければREADYを落としてtrue。stateのみ。 |
| `karqri_task_resume` | 非SUSPENDEDならfalse。bit解除し、wait中でなければREADYを立ててtrue。stateのみ。 |
| `karqri_task_priority` | priorityのuint32_t値を返す。 |
| `karqri_task_set_priority` | priorityを保存して旧値を返す。Ready queueを並べ替えない。登録TaskはScheduler/portとの整合性をcallerが満たす。 |
| `karqri_task_set_delayed_return` | value（0も可）を保存してvalidをtrueにする。前の値を置換。 |
| `karqri_task_has_delayed_return` | 遅延結果validを返す。 |
| `karqri_task_delayed_return` | 保存値をuintptr_tで返す。読取りでconsumeしない。有効な完了値として使う前にvalidを確認。 |
| `karqri_task_clear_delayed_return` | validだけをfalseへ。保存値は保持。 |

**宣言と検証の区別**：上記は全て実装あり。次は当該test/validation source内の直接参照の有無（実行成功や全分岐coverageの意味ではない）。

直接参照あり：`karqri_task_init`、`karqri_task_state`、`karqri_task_state_replace`、`karqri_task_state_set`、`karqri_task_state_clear`、`karqri_task_state_has_any`、`karqri_task_state_has_all`、`karqri_task_is_waiting`、`karqri_task_suspend`、`karqri_task_resume`、`karqri_task_priority`、`karqri_task_set_priority`、`karqri_task_set_delayed_return`、`karqri_task_has_delayed_return`、`karqri_task_delayed_return`、`karqri_task_clear_delayed_return`。

## 6. Event

宣言：[include/karqri/event.h](../include/karqri/event.h)。実装：[kernel/event.c](../kernel/event.c)。検証定義：[tests/test_event.c](../tests/test_event.c)。

raw操作は非blocking。BLOCKEDはwaiter登録を意味し、呼出し元stackを待たせない。timeout引数なし。callbackを含むwait/signal全体を保護。

```c
void karqri_event_init(KarqriEvent *event);

KarqriEventWaitResult karqri_event_wait(
    KarqriEvent *event,
    KarqriEventWaiter *waiter,
    uint32_t requested_events,
    KarqriEventWaitMode mode,
    void *owner,
    const KarqriEventWaitOperations *operations);

void karqri_event_signal(KarqriEvent *event, uint32_t signaled_events);

uint32_t karqri_event_pending(const KarqriEvent *event);
```

| API | 動作・引数・戻り値・固有制約 |
| --- | --- |
| `karqri_event_init` | pending=0、waiter queue空へ。通知callbackなし。waiter/通知元が残る再初期化は禁止。 |
| `karqri_event_wait` | requested_eventsは非0、modeはANY/ALL。即時成立なら消費前pending全snapshotを返してrequested bitsだけ消費し、waiter/callbackに触れない。不成立ならwaiterを設定しwait_started→FIFO登録、BLOCKED/events=0を返す。これは登録結果でありCPUはblockしない。ownerはcallbackが許せばNULL、operationsと3callbackは即時成立の場合も有効な非NULLを要求。timeout/cancelなし。 |
| `karqri_event_signal` | signaled_eventsをpendingへOR（0可）。FIFO走査で不成立waiterをskip。全waiterを同一snapshotで評価するので1回のsignalで複数完了し得る。成立ごとにreturn_value_set（リンク中）→requested bits消費→detach→wait_completed。戻り値なし。snapshotは要求maskとのANDに限定されない。 |
| `karqri_event_pending` | pendingの非消費read。共有readも保護対象。 |

**宣言と検証の区別**：上記は全て実装あり。次は当該test/validation source内の直接参照の有無（実行成功や全分岐coverageの意味ではない）。

直接参照あり：`karqri_event_init`、`karqri_event_wait`、`karqri_event_signal`、`karqri_event_pending`。

## 7. Timer

宣言：[include/karqri/timer.h](../include/karqri/timer.h)。実装：[kernel/timer.c](../kernel/timer.c)。検証定義：[tests/test_timer.c](../tests/test_timer.c)。

raw操作は非blockingだがtick中にexpiryが同期実行される。delay単位はtick呼出し回数で実時間保証なし。同queueへのcallback再入禁止。

```c
void karqri_timer_queue_init(
    KarqriTimerQueue *queue,
    KarqriTimer *timers,
    size_t timer_count);

bool karqri_timer_start(
    KarqriTimerQueue *queue,
    void *destination,
    uint32_t events,
    uint32_t delay_ticks,
    KarqriTimerExpiry expiry);

bool karqri_timer_cancel(
    KarqriTimerQueue *queue,
    void *destination,
    uint32_t events);

void karqri_timer_tick(KarqriTimerQueue *queue);
```

| API | 動作・引数・戻り値・固有制約 |
| --- | --- |
| `karqri_timer_queue_init` | queueのactiveを空、timers[0..timer_count)をfreeへ登録。count=0のみtimers=NULL可。全体再初期化はtick/callback/全外部アクセス停止後だけ。 |
| `karqri_timer_start` | destination/events/expiryを登録しdelay_ticks回のtick後に1回満了。delayは1..UINT32_MAX（0は契約外）。expiryは非NULL、destinationはcallbackが許せばNULL。true=登録、false=pool枯渇（active不変）。同期限は登録順。ここではcallback/dispatchしない。 |
| `karqri_timer_cancel` | destination pointerとevents全値に一致するactive順最初の1件だけ取消し。後続delta補正、callbackなしでfreeへ。true=削除、false=対象なし。expiryは検索keyではない。Event pending/waitは取消さない。 |
| `karqri_timer_tick` | 論理1tick進め、同時満了はFIFO。activeから外す→同期expiry→freeへ戻す。callback中、そのentryは再利用不能。空queueはno-op。周期再登録・時間補償・CPU切替なし。 |

**宣言と検証の区別**：上記は全て実装あり。次は当該test/validation source内の直接参照の有無（実行成功や全分岐coverageの意味ではない）。

直接参照あり：`karqri_timer_queue_init`、`karqri_timer_start`、`karqri_timer_cancel`、`karqri_timer_tick`。

## 8. Scheduler

宣言：[include/karqri/scheduler.h](../include/karqri/scheduler.h)。実装：[kernel/scheduler.c](../kernel/scheduler.c)。検証定義：[tests/test_scheduler.c](../tests/test_scheduler.c)。

internal/integration。全操作は論理状態のみ、CPU切替なし。対象Taskは同scheduler所属。callback/port handoffとの直列化はcaller。

```c
void karqri_scheduler_init(KarqriScheduler *scheduler);

KarqriTask *karqri_scheduler_current(const KarqriScheduler *scheduler);

KarqriTask *karqri_scheduler_candidate(const KarqriScheduler *scheduler);

bool karqri_scheduler_reschedule(KarqriScheduler *scheduler);

bool karqri_scheduler_set_reschedule_suppressed(
    KarqriScheduler *scheduler,
    bool suppressed);

void karqri_scheduler_add_ready(
    KarqriScheduler *scheduler,
    KarqriTask *task);

void karqri_scheduler_begin_wait(
    KarqriScheduler *scheduler,
    KarqriTask *task,
    KarqriTaskState wait_state);

void karqri_scheduler_ready(
    KarqriScheduler *scheduler,
    KarqriTask *task,
    KarqriTaskState completed_wait_state);

bool karqri_scheduler_suspend(
    KarqriScheduler *scheduler,
    KarqriTask *task);

bool karqri_scheduler_resume(
    KarqriScheduler *scheduler,
    KarqriTask *task);

uint32_t karqri_scheduler_set_priority(
    KarqriScheduler *scheduler,
    KarqriTask *task,
    uint32_t priority);

bool karqri_scheduler_rotate_timeslice(KarqriScheduler *scheduler);

bool karqri_scheduler_rotate_task_timeslice(
    KarqriScheduler *scheduler,
    KarqriTask *task);

void karqri_scheduler_remove_task(
    KarqriScheduler *scheduler,
    KarqriTask *task);
```

| API | 動作・引数・戻り値・固有制約 |
| --- | --- |
| `karqri_scheduler_init` | 未使用schedulerを空Ready/Suspended、current=NULL、suppression=falseに初期化。 |
| `karqri_scheduler_current` | 最後に論理選択したTask、なければNULL。CPU実行contextではない。 |
| `karqri_scheduler_candidate` | Ready先頭、空ならNULL。suppression中はcurrentと異なり得る。 |
| `karqri_scheduler_reschedule` | suppressionならfalse。candidateへcurrentを更新し、新currentのstate全体をRUNNINGへ置換。旧Taskのstateは直さない。true=論理選択変更（NULLへの変更も含む）、CPU切替なし。 |
| `karqri_scheduler_set_reschedule_suppressed` | suppressed boolを保存し旧boolを返す。nest countではない。解除だけではrescheduleしない。queue更新は継続。割込み禁止・mutexではない。 |
| `karqri_scheduler_add_ready` | 初回は初期化済みdetached taskを登録。既存Ready Taskなら外して同順位末尾へ再配置しREADY設定、reschedule。再登録は冪等ではない。他scheduler所有は不可。 |
| `karqri_scheduler_begin_wait` | Ready taskを外しstate全体をwait_stateへ置換、reschedule。waiter管理は別。登録・state・handoff全体の保護が必要。 |
| `karqri_scheduler_ready` | completed_wait_stateを解除。wait bitsが残らなければ同順位末尾へ再配置しREADY設定。SUSPENDEDはwait bitではなくREADYと共存し得る。rescheduleする。 |
| `karqri_scheduler_suspend` | task=NULLはcurrent。currentなし/既にSUSPENDEDならfalse。wait中は登録を維持、非waitならReadyからSuspendedへ。成功true、reschedule。実行Taskがself-suspendするには別runnable contextが必要。 |
| `karqri_scheduler_resume` | task=NULL/非SUSPENDEDならfalse。SUSPENDED解除、waitが残れば継続、それ以外はready経由で再配置しtrue。 |
| `karqri_scheduler_set_priority` | Ready taskのみ。task=NULLは既存currentが必須。旧priorityを返す。同値はqueue位置/currentとも不変、変更は新priority群末尾へ再配置しreschedule。wait/suspend中の変更は契約外。 |
| `karqri_scheduler_rotate_timeslice` | currentを同順位末尾へ回す。NULL/detachedはfalse。戻り値は選択変更でありqueue移動の有無ではない。 |
| `karqri_scheduler_rotate_task_timeslice` | 明示task（例：tickに割り込まれたTask）を同順位末尾へ回してreschedule。NULL/detachedはfalse。Ready所属のみ。戻り値は論理選択変更。 |
| `karqri_scheduler_remove_task` | Taskを外しstate=0。currentなら先にNULLへしてreschedule。Event waitは事前に正常完了が必須。waiter取消し、遅延値clear、context破棄やメモリ解放はしない。 |

**宣言と検証の区別**：上記は全て実装あり。次は当該test/validation source内の直接参照の有無（実行成功や全分岐coverageの意味ではない）。

直接参照あり：`karqri_scheduler_init`、`karqri_scheduler_current`、`karqri_scheduler_candidate`、`karqri_scheduler_reschedule`、`karqri_scheduler_set_reschedule_suppressed`、`karqri_scheduler_add_ready`、`karqri_scheduler_suspend`、`karqri_scheduler_resume`、`karqri_scheduler_set_priority`、`karqri_scheduler_rotate_timeslice`、`karqri_scheduler_rotate_task_timeslice`、`karqri_scheduler_remove_task`。

このtest内の直接参照なし：`karqri_scheduler_begin_wait`、`karqri_scheduler_ready`。内部呼出し・他test/demoでの利用とは区別し、単独試験済みとはしない。

## 9. Scheduler/Event adapter

宣言：[include/karqri/scheduler_event.h](../include/karqri/scheduler_event.h)。実装：[kernel/scheduler_event.c](../kernel/scheduler_event.c)。検証定義：[tests/test_scheduler.c](../tests/test_scheduler.c)。

integration。戻り値はraw EventWaitResult。CPU blockはport担当。scheduler/task/waiter/eventすべて非NULLで寿命を保証。

```c
KarqriEventWaitResult karqri_scheduler_wait_event(
    KarqriScheduler *scheduler,
    KarqriTask *task,
    KarqriScheduledEventWaiter *waiter,
    KarqriEvent *event,
    uint32_t requested_events,
    KarqriEventWaitMode mode);
```

| API | 動作・引数・戻り値・固有制約 |
| --- | --- |
| `karqri_scheduler_wait_event` | Ready所属taskとcaller所有waiterをEventへ接続。即時完了はstateと既存遅延結果validを維持。blockではvalidを落としてEVENT_WAITへ置換しReadyから外す。signal完了はsnapshotをvalid遅延値へ保存→EVENT_WAIT解除/ready。CPU切替なし。1Taskにつきactive登録は最大1、途中取消しなし。 |

**宣言と検証の区別**：上記は全て実装あり。次は当該test/validation source内の直接参照の有無（実行成功や全分岐coverageの意味ではない）。

直接参照あり：`karqri_scheduler_wait_event`。

## 10. Timer/Event adapter

宣言：[include/karqri/timer_event.h](../include/karqri/timer_event.h)。実装：[kernel/timer_event.c](../kernel/timer_event.c)。検証定義：[tests/test_timer_event.c](../tests/test_timer_event.c)。

integration。Timer/Eventそれぞれの全契約が適用される。timer_event単体はTask/contextを管理しない。

```c
void karqri_timer_signal_event(void *destination, uint32_t events);
```

| API | 動作・引数・戻り値・固有制約 |
| --- | --- |
| `karqri_timer_signal_event` | destinationは有効な非NULL KarqriEvent*。eventsをそのままsignal。複数同tick満了も個別signalで合成しない。CPU切替なし。元Timerへの間接再入も禁止。 |

**宣言と検証の区別**：上記は全て実装あり。次は当該test/validation source内の直接参照の有無（実行成功や全分岐coverageの意味ではない）。

直接参照あり：なし（内部経路からの間接呼出しは別）。

このtest内の直接参照なし：`karqri_timer_signal_event`。内部呼出し・他test/demoでの利用とは区別し、単独試験済みとはしない。

## 11. POSIX context/dispatch

宣言：[ports/posix/posix_port.h](../ports/posix/posix_port.h)。実装：[ports/posix/posix_port.c](../ports/posix/posix_port.c)。検証定義：[tests/test_posix_kernel.c](../tests/test_posix_kernel.c)。

すべてowner threadのみ。init/create/runは固定groupのhost setup/run、destroyは全終了後host teardown。yieldは実行Task。dispatchは外側callback処理完了後のみ。32 trampoline slotsはprocess-localであり複数並行portを保証しない。

```c
void karqri_posix_port_init(
    KarqriPosixPort *port,
    KarqriScheduler *scheduler);

bool karqri_posix_task_context_create(
    KarqriPosixPort *port,
    KarqriPosixTaskContext *context,
    KarqriTask *task,
    size_t stack_size);

void karqri_posix_task_context_destroy(
    KarqriPosixTaskContext *context);

KarqriTask *karqri_posix_executing_task(const KarqriPosixPort *port);

bool karqri_posix_dispatch(KarqriPosixPort *port);

bool karqri_posix_yield(KarqriPosixPort *port);

bool karqri_posix_run(KarqriPosixPort *port);
```

| API | 動作・引数・戻り値・固有制約 |
| --- | --- |
| `karqri_posix_port_init` | owner threadで未使用portと初期化済みschedulerを関連付け、context表/実行状態を空へ。live port再初期化は禁止。 |
| `karqri_posix_task_context_create` | Task用stackをmallocしgetcontext/makecontext、slot登録、Scheduler add_ready。stack_sizeはbytes。32枠/slot上限、size=0、malloc/getcontext失敗ならfalse。成功true。Task/context重複登録や実行中createは契約外。失敗後未初期化contextへのdestroy不可。 |
| `karqri_posix_task_context_destroy` | 全group正常終了後、owner hostから成功create済みcontextのみ破棄。stack解放・trampoline slot解除・active=false。Task除去、port.contextsからのpointer削除、context_count減算、wait取消しはしない。context/Task保存域は旧port参照がなくなるまで保持。 |
| `karqri_posix_executing_task` | executing_contextのTask、なければNULL。owner threadのみ（readも同じ）。 |
| `karqri_posix_dispatch` | Scheduler currentをnative contextへ反映する。targetなし/contextなし/同じcontextならfalse。swapcontext成功経路から復帰してtrue、失敗時abort。rescheduleはしない。Task内からは再選択されるまで呼出しが戻らない場合がある。outer Event/Timer途中のdispatch禁止。 |
| `karqri_posix_yield` | Scheduler currentをrotateし、選択変更時だけdispatch。true=dispatch経路、同順位peerなし等はfalse。Taskで使用。hardware preemptionではない。 |
| `karqri_posix_run` | owner hostから固定groupを実行。runningならfalse、他はdispatchの戻り値。trueはjoin成功ではない。全context.finished/外部参照を別途確認。Readyなしでもunfinished wait/suspendが残る実装経路はサポート対象の終了ではない。 |

**宣言と検証の区別**：上記は全て実装あり。次は当該test/validation source内の直接参照の有無（実行成功や全分岐coverageの意味ではない）。

直接参照あり：`karqri_posix_port_init`、`karqri_posix_task_context_create`、`karqri_posix_task_context_destroy`、`karqri_posix_yield`、`karqri_posix_run`。

このtest内の直接参照なし：`karqri_posix_executing_task`、`karqri_posix_dispatch`。内部呼出し・他test/demoでの利用とは区別し、単独試験済みとはしない。

## 12. POSIX Kernel wrappers

宣言：[ports/posix/posix_kernel.h](../ports/posix/posix_kernel.h)。実装：[ports/posix/posix_kernel.c](../ports/posix/posix_kernel.c)。検証定義：[tests/test_posix_kernel.c](../tests/test_posix_kernel.c)。

すべてowner thread同期呼出し。waitは実行Taskのみ。signal/tick/resume等もforeign-threadやsignal handlerからは禁止。wrapperを通してもraw共有primitiveの全てがthread-safeになるわけではない。

```c
uintptr_t karqri_posix_wait_event(
    KarqriPosixPort *port,
    KarqriScheduledEventWaiter *waiter,
    KarqriEvent *event,
    uint32_t requested_events,
    KarqriEventWaitMode mode);

void karqri_posix_signal_event(
    KarqriPosixPort *port,
    KarqriEvent *event,
    uint32_t events);

void karqri_posix_timer_tick(
    KarqriPosixPort *port,
    KarqriTimerQueue *timer_queue);

bool karqri_posix_suspend_task(
    KarqriPosixPort *port,
    KarqriTask *task);

bool karqri_posix_resume_task(
    KarqriPosixPort *port,
    KarqriTask *task);
```

| API | 動作・引数・戻り値・固有制約 |
| --- | --- |
| `karqri_posix_wait_event` | 実行Task、suppression=false、block後も別runnable contextが必須。即時snapshotをuintptr_tで返す。blockはdispatchし、再開時のvalid遅延値を返す。timeoutなし。実行Taskなし/復帰時invalidでは実装は0を返すが、正常な待機失敗契約ではない。対応外を必ず診断する保証なし。 |
| `karqri_posix_signal_event` | eventへsignalを完了してからdispatch。eventsはEvent mask。owner thread、outer traversal再入なし。戻り値なし。 |
| `karqri_posix_timer_tick` | Timer tickと全expiryを完了してからdispatch。owner threadから明示呼出し。signal handler tickや実時間駆動のAPIではない。 |
| `karqri_posix_suspend_task` | scheduler_suspend成功時dispatch、boolはsuspend状態変更の成否。task=NULLは論理current。self-suspendは別contextとsuppression=falseが必要。 |
| `karqri_posix_resume_task` | scheduler_resume成功時dispatch、boolはresume状態変更の成否。NULLはfalse。owner threadのみ。 |

**宣言と検証の区別**：上記は全て実装あり。次は当該test/validation source内の直接参照の有無（実行成功や全分岐coverageの意味ではない）。

直接参照あり：`karqri_posix_wait_event`、`karqri_posix_signal_event`、`karqri_posix_timer_tick`、`karqri_posix_suspend_task`、`karqri_posix_resume_task`。

## 13. Pico context

宣言：[ports/pico/pico_context.h](../ports/pico/pico_context.h)。実装：[ports/pico/pico_context.c](../ports/pico/pico_context.c)。検証定義：[ports/pico/validation_main.c](../ports/pico/validation_main.c)。

port構築/検査用。hardware/software frameは各8 uint32、合計64bytes。saved_pspはsoftware frame先頭。無効contextに検査関数を使っても安全とはしない。

```c
bool karqri_pico_context_build(
    KarqriPicoTaskContext *context,
    KarqriTask *task,
    uint32_t *stack_base,
    size_t stack_words,
    void (*bootstrap)(KarqriPicoTaskContext *context),
    void (*return_trap)(void));

bool karqri_pico_context_contains_psp(
    const KarqriPicoTaskContext *context);
```

| API | 動作・引数・戻り値・固有制約 |
| --- | --- |
| `karqri_pico_context_build` | caller提供stack_words個のuint32_t領域へ64byte初期frameを構築。topを8byte境界へ切下げ、R0=context、PC=bootstrap、LR=return_trap、Thumb xPSR。NULL引数/整列後領域不足等でfalse、成功true。割当・Scheduler登録なし。十分なstack量はcaller責務（64byteは実行余裕の保証ではない）。 |
| `karqri_pico_context_contains_psp` | saved_pspがstack_low以上stack_high未満、8byte整列ならtrue。現在のCPU PSP取得、frame全体範囲・guard・high-water検査ではない。有効なcontext/stackが前提。 |

**宣言と検証の区別**：上記は全て実装あり。次は当該test/validation source内の直接参照の有無（実行成功や全分岐coverageの意味ではない）。

直接参照あり：`karqri_pico_context_contains_psp`。

このtest内の直接参照なし：`karqri_pico_context_build`。内部呼出し・他test/demoでの利用とは区別し、単独試験済みとはしない。

## 14. Pico port

宣言：[ports/pico/pico_port.h](../ports/pico/pico_port.h)。実装：[ports/pico/pico_port.c](../ports/pico/pico_port.c)。検証定義：[ports/pico/validation_main.c](../ports/pico/validation_main.c)。

applicationが利用するinit/create/start/yield/executing/suppressionと、assembly/integration内部入口を同headerで宣言している。後者を直接呼ぶAPI例は提供しない。active/pendingはcontext pointer、Scheduler currentはTask pointer。

```c
void karqri_pico_port_init(
    KarqriPicoPort *port,
    KarqriScheduler *scheduler,
    uint32_t slice_ticks);

bool karqri_pico_task_context_create(
    KarqriPicoPort *port,
    KarqriPicoTaskContext *context,
    KarqriTask *task,
    uint32_t *stack_base,
    size_t stack_words);

bool karqri_pico_start(KarqriPicoPort *port);

void karqri_pico_require_running(KarqriPicoPort *port);

bool karqri_pico_request_switch(KarqriPicoPort *port);

KarqriPicoTaskContext *karqri_pico_deliver_event_return(
    KarqriPicoTaskContext *next);

bool karqri_pico_yield(KarqriPicoPort *port);

KarqriTask *karqri_pico_executing_task(void);

bool karqri_pico_set_reschedule_suppressed(
    KarqriPicoPort *port,
    bool suppressed);

void karqri_pico_task_bootstrap(KarqriPicoTaskContext *context);

void karqri_pico_task_return_trap(void);
```

| API | 動作・引数・戻り値・固有制約 |
| --- | --- |
| `karqri_pico_port_init` | scheduler、slice_ticks、実行coreを保持。context表空、started=false。slice_ticks=0はtickによるslice rotationなし。単一runtime、未使用portに対して開始前に使用。 |
| `karqri_pico_task_context_create` | caller所有stack/contextへcontext_buildし、slice残量設定、context表登録、Scheduler add_ready。最大16Task。枠上限/build失敗でfalse、成功true。Task・context・stackを終了と参照消滅まで保持。destroy APIはない。 |
| `karqri_pico_start` | 同一core、privileged Thread/MSP、入口PRIMASK=0。初回SVCでTaskへ移行し全Task正常return後mainへtrue。既にruntime使用/started/currentなし/対応contextなしはfalse、実行context違反はRelease fault。初回bootstrapでRUNNING公開、最終returnで非RUNNING化。hardware alarm開始/停止は別API。全Taskのwait/suspendを解消して終了する。 |
| `karqri_pico_require_running` | 内部guard。callerはPRIMASK保持。active_port一致、started、同一coreを検査し違反はfault。application呼出し用ではない。 |
| `karqri_pico_request_switch` | port内部/integration。PRIMASK保存型でScheduler currentからnext選択→pending/ICSR SET/CLRまで保護。true=要求維持/発行、実切替完了ではない。current更新はcallerの先行Scheduler処理による。周囲のkernel更新全体の保護を代行しない。同一activeでも未配送完了値があれば自己PendSV。 |
| `karqri_pico_deliver_event_return` | assembly専用。PRIMASK下、旧saved_psp更新後にnext frame R0へvalid遅延値を配送しcontext Dのみ解除、nextを返す。Task validは維持。不正なinvalid結果はfault。直接呼出し不可。 |
| `karqri_pico_yield` | RUNNING/same core。PRIMASK保存型でcurrentのtimeslice rotateと要求更新を実施。true=選択変更に伴う切替要求、false=変更なし等。Task利用を基本としISR利用の一般保証は未確認。 |
| `karqri_pico_executing_task` | active_contextのTask、なければNULL。論理currentとは別。read自体にPRIMASK保護なし。他core/外部同時readの保証なし。 |
| `karqri_pico_set_reschedule_suppressed` | RUNNING/same core、PRIMASK保存型。旧boolを返す。trueで既存要求取消し、falseでrescheduleして最新要求再評価。active未完了wait handoffを取り消すsuppressionは変更前fault。割込み禁止ではない。 |
| `karqri_pico_task_bootstrap` | 内部初期PC。Task entry実行とreturn処理。Thread/PSP・active整合を確認し初回started=true公開。returnはmask=0/suppression=false必須。finished設定/Task除去/次要求を保護し、全完了後started=false・要求取消し→最終SVC。未終了contextが残るとfault。直接呼出し不可。 |
| `karqri_pico_task_return_trap` | 内部初期frame LRの予備trap。tight_loop_contentsの無限loop。正常終了処理はbootstrapが担い、ここへ戻る設計ではない。 |

**宣言と検証の区別**：上記は全て実装あり。次は当該test/validation source内の直接参照の有無（実行成功や全分岐coverageの意味ではない）。

直接参照あり：`karqri_pico_port_init`、`karqri_pico_task_context_create`、`karqri_pico_start`、`karqri_pico_yield`、`karqri_pico_set_reschedule_suppressed`。

このtest内の直接参照なし：`karqri_pico_require_running`、`karqri_pico_request_switch`、`karqri_pico_deliver_event_return`、`karqri_pico_executing_task`、`karqri_pico_task_bootstrap`、`karqri_pico_task_return_trap`。内部呼出し・他test/demoでの利用とは区別し、単独試験済みとはしない。

## 15. Pico Kernel wrappers

宣言：[ports/pico/pico_kernel.h](../ports/pico/pico_kernel.h)。実装：[ports/pico/pico_kernel.c](../ports/pico/pico_kernel.c)。検証定義：[ports/pico/validation_main.c](../ports/pico/validation_main.c)。

waitだけはassembly定義（pico_switch.S）。EventWaitCallはport/waiter/event/requested_events/mode/blocked。signal/suspend/resumeはC wrapper。入口条件と対応範囲は関数ごとに異なる。

```c
uint32_t karqri_pico_event_wait(KarqriPicoEventWaitCall *call);

uint32_t karqri_pico_event_wait_prepare(KarqriPicoEventWaitCall *call);

void karqri_pico_event_signal(
    KarqriPicoPort *port,
    KarqriEvent *event,
    uint32_t events);

bool karqri_pico_suspend_task(
    KarqriPicoPort *port,
    KarqriTask *task);

bool karqri_pico_resume_task(
    KarqriPicoPort *port,
    KarqriTask *task);
```

| API | 動作・引数・戻り値・固有制約 |
| --- | --- |
| `karqri_pico_event_wait` | callと全参照先を有効に保つ。privileged Thread/PSP、RUNNING、入口PRIMASK=0。call.blockedは内部出力。判定/登録/Scheduler/要求を一つのmask区間へ。即時はsnapshot、blockはPendSV経由R0 snapshotをuint32_tで返す。timeoutなし。blockで有効要求なし、未配送復帰、context/mask違反はRelease診断停止。busy loopなし。 |
| `karqri_pico_event_wait_prepare` | assembly wrapper専用。外側PRIMASK下で操作。即時はsnapshot、blockはD/blocked設定と切替要求後に内部仮値0を返す。applicationはこの仮値を受け取ってはならない。 |
| `karqri_pico_event_signal` | RUNNING/same core。保存型PRIMASKでsignal全体/callback/要求更新を直列化し元maskへ。Taskとport tick IRQ連携の経路あり。任意ISR/NMI/他coreまで安全という保証ではない。再入禁止はEvent契約を継続。 |
| `karqri_pico_suspend_task` | RUNNING/same core、Scheduler suspendと要求をPRIMASKで保護。task=NULLは論理current。boolは状態変更。self-suspendの別runnable context確保・suppressionなしはcaller条件。任意ISRからのself操作は保証しない。 |
| `karqri_pico_resume_task` | RUNNING/same core、Scheduler resumeと要求をPRIMASKで保護。NULL/非suspendedはfalse。boolは状態変更。任意ISRでの対応範囲は一般化せず、tick以外の独立試験は未確認。 |

**宣言と検証の区別**：上記は全て実装あり。次は当該test/validation source内の直接参照の有無（実行成功や全分岐coverageの意味ではない）。

直接参照あり：`karqri_pico_event_wait`、`karqri_pico_event_signal`、`karqri_pico_suspend_task`、`karqri_pico_resume_task`。

このtest内の直接参照なし：`karqri_pico_event_wait_prepare`。内部呼出し・他test/demoでの利用とは区別し、単独試験済みとはしない。

## 16. Pico hardware tick

宣言：[ports/pico/pico_tick.h](../ports/pico/pico_tick.h)。実装：[ports/pico/pico_tick.c](../ports/pico/pico_tick.c)。検証定義：[ports/pico/validation_main.c](../ports/pico/validation_main.c)。

hardware alarm所有期間とRUNNING期間は別。irq_countはraw callback、kernel_ticksはRUNNING中のみ、switch_requestsは要求成功回数（切替回数ではない）、missed_rearmsは累積miss、consecutive_missesは連続miss。uint32 counterの無限期間非wrap保証はない。callbackごと相対再arm、最初のkernel tickは既存alarm位相を受け入れる。時間補償なし。再arm missはforce IRQ、成功で連続count=0、既定8回まで許容・9回目fault。上限はbuild時macroで変更可の暫定budgetでありThread/PendSV進行保証ではない。

```c
bool karqri_pico_tick_start(
    KarqriPicoTick *tick,
    KarqriPicoPort *port,
    KarqriTimerQueue *timer_queue,
    uint32_t period_us);

void karqri_pico_tick_stop(KarqriPicoTick *tick);
```

| API | 動作・引数・戻り値・固有制約 |
| --- | --- |
| `karqri_pico_tick_start` | 同一core privileged Thread専用、callerの既存PRIMASKを復元。period_us>0、tick/port/timer_queue非NULL。使用中tick/未使用alarmなし/初回arm missedはfalse。初回missはforceせず登録・IRQ要因/pending/所有をcleanup。成功trueでhardware alarm稼働、started=false中はkernelへ触れずraw callbackのみ計上。 |
| `karqri_pico_tick_stop` | 同一core privileged Thread専用。保存型PRIMASK下にalarm cancel→callback解除→peripheral要因解除→NVIC pending解除→registry解除→unclaim。未登録/開始失敗後/nullはregistry上no-op。戻り値なし。stop完了までstorageを保持、IRQ/callback再入・他core呼出しは禁止。 |

**宣言と検証の区別**：上記は全て実装あり。次は当該test/validation source内の直接参照の有無（実行成功や全分岐coverageの意味ではない）。

直接参照あり：`karqri_pico_tick_start`、`karqri_pico_tick_stop`。

## 17. 内部ヘッダー・symbolsの補足（application APIではない）

- [pico_interrupt.h](../ports/pico/pico_interrupt.h)：`static inline uint32_t karqri_pico_interrupt_save_and_disable(void)`はMRS PRIMASK→CPSID Iで旧maskを返す。`static inline void karqri_pico_interrupt_restore(uint32_t primask)`は保存値をMSRで戻す。同一core専用、nest時も各保存値を使う。switch手配はしない。復元helper自体にはISBがない（wait wrapperが別途実行）。
- [pico_fault.h](../ports/pico/pico_fault.h)：`_Noreturn void karqri_pico_fault(uint32_t reason)`はvolatile `karqri_pico_fault_reason`を記録しDSB/UDF。Releaseでも有効で戻らない。正常なerror-return APIではない。1=WAIT_PRIMASK、2=WAIT_CONTEXT、3=HANDOFF、4=MSP_ALIGNMENT、5=SUPPRESSION、6=DELAYED_RETURN、7=PORT_CONTEXT、8=PORT_STATE、9=UNFINISHED_TASKS、10=TICK_CONTEXT、11=TICK_MISSED。USBへfaultを自動出力する保証なし。SWD等でreason取得。
- [pico_context_offsets.h](../ports/pico/pico_context_offsets.h)、[pico_kernel_offsets.h](../ports/pico/pico_kernel_offsets.h)：frame/register/context D/blockedのCとassembly間ABI定数。static assertで検査。アプリからoffsetを直接書き換えない。
- `karqri_pico_active_context`、`karqri_pico_pending_context`、`karqri_pico_switch_count`はpico_port.hのextern。activeは物理handoff状態、pendingは未消費の次context、counterは非空PendSV（自己切替を含む）で増加。外部から代入しない。counterはTask命令実行数ではない。

### 17.1 Private cooperative trace

[pico_trace.h](../ports/pico/pico_trace.h)は明示的にprivate。`KARQRI_PICO_TRACE=0`ではbuffer/記録処理を除去し、begin/freeze/dumpマクロはno-op。=1では次を定義する。

```c
void karqri_pico_trace_begin(KarqriPicoPort *port);
void karqri_pico_trace_freeze(void);
void karqri_pico_trace_dump(const char *labels);
void karqri_pico_trace_record(uint16_t event, uint8_t source,
                             uintptr_t subject, uint32_t value);
void karqri_pico_trace_commit(void);
```

beginは有効portのcapture状態とsequence/counter/開始時刻をreset。freezeはcapture停止と終了時刻取得。dumpはfreeze後main側でlabels[context_count]分を読みUSB/stdioへまとめて出力（capture中/portなしは何も出さない）。recordはcapture中だけ保存型PRIMASKで固定ringへ追加し、満杯なら古いrecordを上書きしてoverwritten加算。seq上限でcapture停止・overflow設定。commitはPendSV専用でold contextとrequest seqを記録。`karqri_pico_trace_old_context`はPendSV用scratch extern。

`KarqriPicoTraceRecord`は32bytes、256件=8192bytes＋管理情報。seq、1µs timer低32bit、active/current/pending/subject/value、event/source/flags。eventはBOOTSTRAP/MARK/YIELD/SET/REPLACE/CANCEL/COMMIT/RETURN/STOP、sourceはUNKNOWN/FROM_YIELD/FROM_RETURN。`KARQRI_TRACE`/`KARQRI_TRACE_ARG`はbuild用マクロ。cooperative_three_task限定の計測であり、任意入力のtrace service、他core/NMI/HardFault記録ではない。dumpは経過時間2^32µs未満かtime_validを表示する。

HANDOFF_COMMITはexception return直前の観測で、Task命令を実行済みとは意味しない。記録自体がstack/latency/IRQ timingに影響する。38件/8handoff/ADGBEHCFI/overwritten=0の既存実機結果では、ログにfirmware hashがなく、実行したbinaryの同一性は独立に確認されていない。

## 18. 最小の使用例

### 18.1 自前TimerからEventを通知する登録

```c
KarqriEvent event;
KarqriTimerQueue timers;
KarqriTimer pool[2];
karqri_event_init(&event);
karqri_timer_queue_init(&timers, pool, 2);
bool registered = karqri_timer_start(
    &timers, &event, UINT32_C(1), 3, karqri_timer_signal_event);
/* falseなら登録されていない。objectは通知完了まで寿命を維持する。
 * これは単独環境の登録例。共有Pico queueの操作全体には別途保護が必要。
 * 論理tickを3回進めるかはport/applicationが決定する。 */
(void)registered;
```

includeは`karqri/event.h`、`karqri/timer.h`、`karqri/timer_event.h`が必要。この例はEvent wait timeoutを構成しない（timeout/cancel結果を返すAPIはない）。

### 18.2 実行・終了の参照例

完全な起動・寿命管理は[POSIX integration test](../tests/test_posix_kernel.c)、[Pico validation](../ports/pico/validation_main.c)、[POSIX demo](../demos/posix/track_demo.c)、[Pico demo](../demos/pico/track_demo.c)を参照。Task init→context create（Scheduler登録込み）→run/start→全Task正常終了と外部参照確認→POSIX destroy / Pico tick stopの順。raw Task initだけでTaskは起動しない。

Picoの通常構築では、Scheduler/port/Event/Timer/Taskを初期化し全contextを作成、必要なTimerを登録してからtick_start→startを呼ぶ。alarmはstart前から動くがRUNNING公開まではkernelへ入らない。tick_start失敗は実行開始の成功として扱わず、startがfalseで戻る場合も所有中alarmのstopを行う。start正常復帰後はtick_stopし、通知/参照の終了を確認してからcaller所有storageを再利用する。具体的な失敗cleanupはvalidation/demoの分岐を参照。

## 19. 検証状況・未確認事項・利用上の注意

| 根拠 | 確認できる範囲 | 拡張してはいけない主張 |
| --- | --- | --- |
| host testsの既存7/7 PASS | Queue/Task/Event/Timer/Timer-Event/Scheduler/POSIX integrationの各test定義。Event再利用、同tick通知等の単体testあり | 個々の全APIの全入力、ISR競合、全実行contextのcoverageではない。既存の実行記録であり、新たな実行結果ではない |
| Pico validationの5phase | single_task、cooperative_three_task、event_delayed_r0、suspend_resume、hardware_preemptionの通常経路。PSP/guard/register検査は各scenario実装の範囲 | 早期wake自己PendSVや全handoff interleavingの実機網羅ではない |
| 補助検証の既存記録 | 初回arm失敗cleanup、miss回復、mask/handoff等にstub/エミュレーション/逆assemblyの記録 | 全ての試験資料が現test suiteに収録されるわけではなく、RP2040実測保証へ転記しない |
| POSIXデモの通常/fast | 明示checkpoint tickの6000進行、6Task正常終了等 | hardware timer preemptionではない。stack headroom測定ではない |
| CDC修正版Pico実機 | ユーザー報告：6000 tick、irq=6000、switches=6027、critical=26,26、waits=22、overlap=0、owner=-1、waiters_empty=YES、guards=intact、PASS | Codex自身の実機操作/hash照合ではない。各API単体や特殊境界の実機PASSではない |

現行対象の関数は全て実装を確認した。宣言だけの未実装public関数は見つからなかった。ただし内部入口や直接test参照のない関数の単独成功、任意ISR対応、未保証の引数domainは未確認。

利用上の注意：

1. Picoには、待機登録の直列化、切替要求とPendSV受渡しの保護、RUNNING状態によるティック有効期間の制御が実装されている。ただし、特殊経路の実機検証がすべて完了したわけではない。共有する基本機能を直接操作する場合の同期責務は呼出し側に残る。[scheduler_event.h](../include/karqri/scheduler_event.h)、[timer.h](../include/karqri/timer.h)、[timer_event.h](../include/karqri/timer_event.h)も参照。
2. POSIX runのbool、context.active、Task RUNNING bitは名前だけから全終了/CPU実行と読めない。header契約通りの制約でありruntime不具合認定ではない。
3. Pico requestはScheduler currentを読んでおり、`scheduler_candidate()`のReady先頭を再選択する関数ではない。callerのScheduler更新と一体で使う。raw操作の保護をrequestだけで置換しない。
4. Pico context_contains_pspはsaved_pspだけの検査。完全frame/stack guard検査と説明してはいけない。任意の巨大stack_words等の全算術domain試験は未確認。
5. POSIXはsuppression中return/残存waiterをPicoのように必ず診断しない。対応外条件を満たさない実行を0やhost復帰の成功で扱わない。

未検証部分が残る対象は反復waiterの実context遅延値、同tick wakeの実行FIFO、早期起床/自己PendSV/P/H置換取消し/空要求、SVC境界IRQ、alarm miss/stop/restart、MSP high-water/IRQ nest、trace影響。APIの正常仕様と検証完了を分ける。設計の背景は[API Design Notes](api-design-ja.md)、通常経路の検証状況は[README](../README-ja.md#検証状況)を参照。
