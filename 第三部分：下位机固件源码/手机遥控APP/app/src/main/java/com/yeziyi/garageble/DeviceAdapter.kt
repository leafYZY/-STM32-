package com.yeziyi.garageble

import android.annotation.SuppressLint
import android.bluetooth.BluetoothDevice
import android.view.LayoutInflater
import android.view.ViewGroup
import androidx.recyclerview.widget.RecyclerView
import com.yeziyi.garageble.databinding.ItemDeviceBinding

/** 蓝牙设备列表适配器（已配对 + 扫描到的） */
class DeviceAdapter(
    private val onClick: (BluetoothDevice) -> Unit
) : RecyclerView.Adapter<DeviceAdapter.VH>() {

    private val items = ArrayList<BluetoothDevice>()

    /** 全量替换（例如列出已配对设备） */
    @SuppressLint("NotifyDataSetChanged")
    fun submit(list: List<BluetoothDevice>) {
        items.clear()
        items.addAll(list)
        notifyDataSetChanged()
    }

    /** 增量添加（扫描过程中逐个发现） */
    fun add(device: BluetoothDevice, showUnbonded: Boolean) {
        if (device.bondState != BluetoothDevice.BOND_BONDED && !showUnbonded) return
        if (items.any { it.address == device.address }) return
        items.add(device)
        notifyItemInserted(items.size - 1)
    }

    fun clear() {
        val n = items.size
        items.clear()
        notifyItemRangeRemoved(0, n)
    }

    override fun onCreateViewHolder(parent: ViewGroup, viewType: Int): VH =
        VH(ItemDeviceBinding.inflate(LayoutInflater.from(parent.context), parent, false))

    override fun getItemCount(): Int = items.size

    @SuppressLint("MissingPermission", "SetTextI18n")
    override fun onBindViewHolder(holder: VH, position: Int) {
        val d = items[position]
        val b = holder.b

        b.tvDevName.text = try {
            d.name ?: "(未知设备)"
        } catch (_: SecurityException) {
            "(未知设备)"
        }
        b.tvDevAddr.text = d.address
        val typeTag = try {
            when (d.type) {
                BluetoothDevice.DEVICE_TYPE_CLASSIC -> "经典"
                BluetoothDevice.DEVICE_TYPE_LE -> "BLE"
                BluetoothDevice.DEVICE_TYPE_DUAL -> "双模"
                else -> "类型未知"
            }
        } catch (_: SecurityException) {
            "类型未知"
        }
        val bonded = d.bondState == BluetoothDevice.BOND_BONDED
        // 两种都能连：经典走 SPP、BLE 走 GATT，只是通道不同
        b.tvDevBonded.text = (if (bonded) "已配对" else "未配对") + " · " + typeTag
        b.tvDevBonded.setTextColor(
            when {
                typeTag == "经典" || typeTag == "双模" -> 0xFF2E7D32.toInt()
                typeTag == "BLE" -> 0xFF1565C0.toInt()
                bonded -> 0xFF1565C0.toInt()
                else -> 0xFF6B7280.toInt()
            }
        )
        b.root.setOnClickListener { onClick(d) }
    }

    class VH(val b: ItemDeviceBinding) : RecyclerView.ViewHolder(b.root)
}
