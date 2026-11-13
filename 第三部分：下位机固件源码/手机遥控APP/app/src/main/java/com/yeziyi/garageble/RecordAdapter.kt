package com.yeziyi.garageble

import android.annotation.SuppressLint
import android.view.LayoutInflater
import android.view.ViewGroup
import androidx.recyclerview.widget.RecyclerView
import com.yeziyi.garageble.databinding.ItemRecordBinding

/** 停车记录列表适配器：最新一条在最上面 */
class RecordAdapter : RecyclerView.Adapter<RecordAdapter.VH>() {

    private val items = ArrayList<ParkingRecord>()

    @SuppressLint("NotifyDataSetChanged")
    fun submit(list: List<ParkingRecord>) {
        items.clear()
        items.addAll(list)
        notifyDataSetChanged()
    }

    override fun onCreateViewHolder(parent: ViewGroup, viewType: Int): VH =
        VH(ItemRecordBinding.inflate(LayoutInflater.from(parent.context), parent, false))

    override fun getItemCount(): Int = items.size

    override fun onBindViewHolder(holder: VH, position: Int) {
        val r = items[position]
        val b = holder.b
        b.tvRecIndex.text = (position + 1).toString()
        b.tvRecPlate.text = r.plate
        b.tvRecTime.text = r.timeText
        b.tvRecSpace.text = r.spaceText
    }

    class VH(val b: ItemRecordBinding) : RecyclerView.ViewHolder(b.root)
}
