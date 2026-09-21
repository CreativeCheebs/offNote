package com.offnote.app

import androidx.room.ColumnInfo
import androidx.room.Dao
import androidx.room.Database
import androidx.room.Entity
import androidx.room.Insert
import androidx.room.PrimaryKey
import androidx.room.Query
import androidx.room.Room
import androidx.room.RoomDatabase
import android.content.Context

/**
 * Durable local outbox, field-for-field aligned with the shared schema in
 * src-tauri/src/store.rs (notes table) so the same delivery/retry semantics
 * apply on-device: every note lands here before any delivery is attempted,
 * and a failed delivery just leaves the row at processed = 0.
 */
@Entity(tableName = "notes")
data class NoteEntity(
    @PrimaryKey(autoGenerate = true) val id: Long = 0,
    @ColumnInfo(name = "created_at") val createdAt: String,
    val data: String,
    val tags: String,
    val targets: String,
    val processed: Boolean = false,
    @ColumnInfo(name = "processed_at") val processedAt: String? = null,
    val error: String? = null,
    val source: String = "android",
)

@Dao
interface NoteDao {
    @Insert
    suspend fun insert(note: NoteEntity): Long

    @Query("SELECT * FROM notes WHERE processed = 0")
    suspend fun pending(): List<NoteEntity>

    @Query("SELECT * FROM notes ORDER BY id DESC LIMIT :limit")
    suspend fun recent(limit: Int): List<NoteEntity>

    @Query("UPDATE notes SET processed = 1, processed_at = :processedAt, error = NULL WHERE id = :id")
    suspend fun markProcessed(id: Long, processedAt: String)

    @Query("UPDATE notes SET targets = :targets WHERE id = :id")
    suspend fun updateTargets(id: Long, targets: String)

    @Query("UPDATE notes SET error = :error WHERE id = :id")
    suspend fun markError(id: Long, error: String)
}

@Database(entities = [NoteEntity::class], version = 1, exportSchema = false)
abstract class NoteDatabase : RoomDatabase() {
    abstract fun noteDao(): NoteDao

    companion object {
        @Volatile private var instance: NoteDatabase? = null

        fun get(context: Context): NoteDatabase =
            instance ?: synchronized(this) {
                instance ?: Room.databaseBuilder(
                    context.applicationContext,
                    NoteDatabase::class.java,
                    "offnote.db",
                ).build().also { instance = it }
            }
    }
}
