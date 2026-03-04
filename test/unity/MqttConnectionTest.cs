/**
 * MqttConnectionTest.cs
 * システム全体像 疎通確認 — Unity 側
 *
 * 確認できること
 *   1. MQTT ブローカへの接続
 *   2. yugo/# の全トピックを subscribe して Console にログ出力
 *   3. Space キーで cmd（LED点灯）を publish
 *
 * セットアップ
 *   1. M2Mqtt を Unity に導入
 *      https://github.com/CE-SDV-Unity/M2MqttUnity
 *      または NuGetForUnity 経由で M2Mqtt をインストール
 *   2. 空の GameObject に本スクリプトをアタッチ
 *   3. Inspector で brokerAddress を管理PC の IP に設定
 *   4. Play → Console ウィンドウを開いて確認
 */

using System;
using System.Collections.Concurrent;
using System.Text;
using UnityEngine;
using uPLibrary.Networking.M2Mqtt;
using uPLibrary.Networking.M2Mqtt.Messages;

public class MqttConnectionTest : MonoBehaviour
{
    [Header("接続設定")]
    [Tooltip("MQTT ブローカ（Mosquitto）が動いている PC の IP")]
    public string brokerAddress = "127.0.0.1";
    public int    brokerPort    = 1883;

    [Header("購読トピック（ワイルドカード可）")]
    [Tooltip("yugo/# で全トピックを受信")]
    public string subscribePattern = "yugo/#";

    [Header("送信テスト（Space キーで送信）")]
    public string cmdTopic   = "yugo/stage1/nodeA/cmd";
    public string cmdPayload = "{\"t\":\"led\",\"id\":\"tree\",\"v\":1}";

    // ────────────────────────────────────────
    // Private
    // ────────────────────────────────────────
    MqttClient _client;

    // ワーカースレッド → メインスレッド転送キュー
    readonly ConcurrentQueue<(string topic, string msg)> _queue = new();

    // ────────────────────────────────────────
    // Unity ライフサイクル
    // ────────────────────────────────────────
    void Start()
    {
        Connect();
    }

    void Update()
    {
        // キューを消費してメインスレッドで処理（GameObject 操作はここで行う）
        while (_queue.TryDequeue(out var item))
        {
            OnMessageMain(item.topic, item.msg);
        }

        // Space キーで cmd 送信テスト
        if (Input.GetKeyDown(KeyCode.Space))
        {
            Publish(cmdTopic, cmdPayload);
        }
    }

    void OnDestroy()
    {
        if (_client is { IsConnected: true })
            _client.Disconnect();
    }

    // ────────────────────────────────────────
    // 接続
    // ────────────────────────────────────────
    void Connect()
    {
        try
        {
            _client = new MqttClient(
                brokerAddress, brokerPort,
                false, null, null, MqttSslProtocols.None);

            _client.MqttMsgPublishReceived += OnMessageReceived;

            string clientId = "unity-test-" + Guid.NewGuid().ToString("N")[..6];
            _client.Connect(clientId);

            if (_client.IsConnected)
            {
                Debug.Log($"[MQTT] Connected to {brokerAddress}:{brokerPort}");
                _client.Subscribe(
                    new[] { subscribePattern },
                    new[] { MqttMsgBase.QOS_LEVEL_AT_MOST_ONCE });
                Debug.Log($"[MQTT] Subscribed: {subscribePattern}");
                Debug.Log("[MQTT] Space キーで cmd 送信テスト");
            }
            else
            {
                Debug.LogError("[MQTT] Connection failed. ブローカのIPとポートを確認してください。");
            }
        }
        catch (Exception ex)
        {
            Debug.LogError($"[MQTT] Exception: {ex.Message}");
        }
    }

    // ────────────────────────────────────────
    // 受信（ワーカースレッド）→ キューに積む
    // ────────────────────────────────────────
    void OnMessageReceived(object sender, MqttMsgPublishEventArgs e)
    {
        string topic = e.Topic;
        string msg   = Encoding.UTF8.GetString(e.Message);
        _queue.Enqueue((topic, msg));
    }

    // ────────────────────────────────────────
    // 受信（メインスレッド） — ここに Unity 処理を追加
    // ────────────────────────────────────────
    void OnMessageMain(string topic, string msg)
    {
        // hb / evt / cmd を色分けしてログ
        if (topic.EndsWith("/hb"))
            Debug.Log($"<color=grey>[HB ] {topic}  {msg}</color>");
        else if (topic.EndsWith("/evt"))
            Debug.Log($"<color=cyan>[EVT] {topic}  {msg}</color>");
        else
            Debug.Log($"<color=yellow>[MSG] {topic}  {msg}</color>");

        // TODO: topic に応じてステージ進行処理を呼ぶ
        // 例: StageManager.Instance.HandleEvent(topic, msg);
    }

    // ────────────────────────────────────────
    // 送信
    // ────────────────────────────────────────
    public void Publish(string topic, string payload)
    {
        if (_client == null || !_client.IsConnected)
        {
            Debug.LogWarning("[MQTT] Not connected.");
            return;
        }
        byte[] bytes = Encoding.UTF8.GetBytes(payload);
        _client.Publish(topic, bytes, MqttMsgBase.QOS_LEVEL_AT_MOST_ONCE, false);
        Debug.Log($"<color=green>[CMD] -> {topic}  {payload}</color>");
    }
}
