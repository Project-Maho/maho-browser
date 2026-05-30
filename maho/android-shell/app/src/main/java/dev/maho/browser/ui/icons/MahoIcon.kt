package dev.maho.browser.ui.icons

import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.automirrored.filled.ArrowBack
import androidx.compose.material.icons.automirrored.filled.ArrowForward
import androidx.compose.material.icons.automirrored.filled.OpenInNew
import androidx.compose.material.icons.filled.Add
import androidx.compose.material.icons.filled.Archive
import androidx.compose.material.icons.filled.AutoAwesome
import androidx.compose.material.icons.filled.Book
import androidx.compose.material.icons.filled.Bookmark
import androidx.compose.material.icons.filled.BookmarkBorder
import androidx.compose.material.icons.filled.ClearAll
import androidx.compose.material.icons.filled.Close
import androidx.compose.material.icons.filled.Code
import androidx.compose.material.icons.filled.Delete
import androidx.compose.material.icons.filled.Download
import androidx.compose.material.icons.filled.Feedback
import androidx.compose.material.icons.filled.FilterList
import androidx.compose.material.icons.filled.Folder
import androidx.compose.material.icons.filled.History
import androidx.compose.material.icons.filled.Key
import androidx.compose.material.icons.filled.KeyboardArrowDown
import androidx.compose.material.icons.filled.Language
import androidx.compose.material.icons.filled.Lock
import androidx.compose.material.icons.filled.MoreVert
import androidx.compose.material.icons.filled.NoteAlt
import androidx.compose.material.icons.filled.Notifications
import androidx.compose.material.icons.filled.Palette
import androidx.compose.material.icons.filled.Person
import androidx.compose.material.icons.filled.Refresh
import androidx.compose.material.icons.filled.RestartAlt
import androidx.compose.material.icons.filled.Schedule
import androidx.compose.material.icons.filled.Search
import androidx.compose.material.icons.filled.Security
import androidx.compose.material.icons.filled.Settings
import androidx.compose.material.icons.filled.Share
import androidx.compose.material.icons.filled.Star
import androidx.compose.material.icons.filled.Sync
import androidx.compose.material.icons.filled.Tab
import androidx.compose.material.icons.filled.Tune
import androidx.compose.material.icons.filled.Visibility
import androidx.compose.ui.graphics.vector.ImageVector

enum class MahoIcon(val imageVector: ImageVector) {
    NavBack(Icons.AutoMirrored.Filled.ArrowBack),
    NavForward(Icons.AutoMirrored.Filled.ArrowForward),
    Close(Icons.Default.Close),
    ReloadSync(Icons.Default.Refresh),
    Search(Icons.Default.Search),
    Globe(Icons.Default.Language),
    Downloads(Icons.Default.Download),
    Archive(Icons.Default.Schedule),
    Spaces(Icons.Default.Tab),
    Boosts(Icons.Default.AutoAwesome),
    Notes(Icons.Default.NoteAlt),
    Bookmarks(Icons.Default.Bookmark),
    History(Icons.Default.History),
    ReadingList(Icons.Default.Visibility),
    Settings(Icons.Default.Settings),
    Profiles(Icons.Default.Person),
    Notifications(Icons.Default.Notifications),
    AutofillKey(Icons.Default.Key),
    Privacy(Icons.Default.Security),
    HelpFeedback(Icons.Default.Feedback),
    ClearSweep(Icons.Default.ClearAll),
    Delete(Icons.Default.Delete),
    Share(Icons.Default.Share),
    MoreActions(Icons.Default.MoreVert),
    InfoLock(Icons.Default.Lock),
    SparklesAi(Icons.Default.AutoAwesome),
    Add(Icons.Default.Add),
    CheckCircle(Icons.Default.Person),
    ExpandMore(Icons.Default.KeyboardArrowDown),
    Filter(Icons.Default.FilterList),
    Folder(Icons.Default.Folder),
    Launch(Icons.AutoMirrored.Filled.OpenInNew),
    SearchAlt(Icons.Default.Search),
    
    // Settings specific icons
    Palette(Icons.Default.Palette),
    SyncIcon(Icons.Default.Sync),
    Book(Icons.Default.Book),
    Star(Icons.Default.Star),
    Tune(Icons.Default.Tune),
    Code(Icons.Default.Code),
    RestartAlt(Icons.Default.RestartAlt);
}
