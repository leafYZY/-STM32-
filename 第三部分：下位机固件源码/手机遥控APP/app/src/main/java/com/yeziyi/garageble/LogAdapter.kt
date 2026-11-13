package com.yeziyi.garageble

import android.annotation.SuppressLint
import android.view.LayoutInflater
import android.view.ViewGroup
import androidx.core.content.ContextCompat
import androidx.recyclerview.widget.RecyclerView
import com.yeziyi.garageble.databinding.ItemLogBinding
import java.text.SimpleDateFormat
import java.util.Date
import java.util.Locale

/** 运行日志适配器：最新一条永远在最上面 */
class LogAdapter : RecyclerView.Adapter<LogAdapter.VH>() {

    enum class Kind { TX, RX, SYS, ERR }

    data class Line(
        val time: String,
        val tag: String,
        val msg: String,
        val kind: Kind
    )

    private val items = ArrayList<Line>()
    private val timeFormat = SimpleDateFormat("HH:mm:ss", Locale.US)

    val size: Int get() = items.size

    /** 必须在主线程调用 */
    fun add(kind: Kind, msg: String) {
        val tag = when (kind) {
            Kind.TX -> "发送"
            Kind.RX -> "接收"
            Kind.SYS -> "系统"
            Kind.ERR -> "错误"
        }
        items.add(0, Line(timeFormat.format(Date()), tag, msg, kind))
        // 日志只保留最近 300 条，避免长时间运行吃掉内存
        if (items.size > MAX_LINES) {
            val removed = items.size
            while (items.size > MAX_LINES) items.removeAt(items.size - 1)
            notifyItemRangeRemoved(MAX_LINES, removed - MAX_LINES)
        }
        notifyItemInserted(0)
    }

    @SuppressLint("NotifyDataSetChanged")
    fun clear() {
        items.clear()
        notifyDataSetChanged()
    }

    override fun onCreateViewHolder(parent: ViewGroup, viewType: Int): VH =
        VH(ItemLogBinding.inflate(LayoutInflater.from(parent.context), parent, false))

    override fun getItemCount(): Int = items.size

    override fun onBindViewHolder(holder: VH, position: Int) {
        val line = items[position]
        val b = holder.b
        val ctx = b.root.context

        b.tvLogTime.text = line.time
        b.tvLogTag.text = line.tag
        b.tvLogMsg.text = line.msg
        b.tvLogMsg.alpha = if (position == 0) 1.0f else 0.72f

        val colorRes = when (line.kind) {
            Kind.TX -> R.color.log_tx
            Kind.RX -> R.color.log_rx
            Kind.SYS -> R.color.log_sys
            Kind.ERR -> R.color.warn_red
        }
        b.tvLogTag.setTextColor(ContextCompat.getColor(ctx, colorRes))
        b.tvLogMsg.setTextColor(
            ContextCompat.getColor(
                ctx,
                if (line.kind == Kind.ERR) R.color.warn_red else R.color.log_text
            )
        )
    }

    class VH(val b: ItemLogBinding) : RecyclerView.ViewHolder(b.root)

    private companion object {
        const val MAX_LINES = 300
    }
}
