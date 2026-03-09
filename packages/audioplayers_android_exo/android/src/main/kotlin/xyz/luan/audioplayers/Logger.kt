package xyz.luan.audioplayers

class Logger(
    private val prefix: String = "",
) {
    fun log(s: String, color: String = ""): Unit {
        val str = "📱 ${colorMap[color]}$prefix$s\u001B[0m";
        println(str);
    }

    fun green(s: String) = log(s, "green")
    fun blue(s: String) = log(s, "blue")
    fun warn(s: String) = log(s, "yellow")
    fun error(s: String) = log(s, "red")
}

val colorMap = mapOf(
    "black" to "\u001B[30m",
    "red" to "\u001B[31m",
    "green" to "\u001B[32m",
    "yellow" to "\u001B[33m",
    "blue" to "\u001B[34m",
    "magenta" to "\u001B[35m",
    "cyan" to "\u001B[36m",
    "white" to "\u001B[37m",
    "reset" to "\u001B[0m",
    "" to "",
)
