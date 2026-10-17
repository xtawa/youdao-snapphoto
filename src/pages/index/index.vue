<template>
  <div class="screen" :class="orientation">
    <div class="header">
      <div class="brand">
        <text class="brand-name">SnapPhoto</text>
        <text class="brand-sub">X7 PRO</text>
      </div>
      <div class="head-actions">
        <text class="state">{{recording?'● REC '+elapsed:(connected?'● READY':'● OFFLINE')}}</text>
        <text class="head-btn" @click="cycleLayout">{{layoutLabel}}</text>
        <text class="head-btn" @click="connect">重连</text>
        <text class="head-btn" @click="exit">退出</text>
      </div>
    </div>
    <div class="layout">
      <div class="sidebar">
        <div class="menu-item" :class="panel==='camera'&&mode==='photo'?'active':''" @click="changeMode('photo')"><text class="menu-icon">◎</text><text class="menu-text">拍照</text></div>
        <div class="menu-item" :class="panel==='camera'&&mode==='video'?'active':''" @click="changeMode('video')"><text class="menu-icon">▣</text><text class="menu-text">录像</text></div>
        <div class="menu-item" :class="panel==='gallery'?'active':''" @click="gallery"><text class="menu-icon">▦</text><text class="menu-text">相册</text></div>
        <div class="menu-item" :class="panel==='share'?'active':''" @click="sharePanel"><text class="menu-icon">⇧</text><text class="menu-text">导出</text></div>
        <div class="menu-item" :class="panel==='settings'?'active':''" @click="settingsPanel"><text class="menu-icon">⚙</text><text class="menu-text">设置</text></div>
      </div>
      <div class="main">
        <div v-if="panel==='camera'" class="camera">
          <div class="view">
            <image v-if="preview" class="preview" :src="preview" resize="contain"/>
            <div v-else class="placeholder"><text class="large-icon">◉</text><text class="placeholder-heading">{{connected?'等待相机取景':'摄像头未连接'}}</text><text class="placeholder-caption">{{connected?'等待真实取景帧':'需要 X7 Pro 原生摄像头适配'}}</text></div>
            <text class="view-badge">{{mode==='photo'?'PHOTO':'VIDEO'}} · {{zoom.toFixed(1)}}×</text>
            <div v-if="showGrid" class="grid-layer">
              <div class="grid-line-h grid-h1"></div><div class="grid-line-h grid-h2"></div>
              <div class="grid-line-v grid-v1"></div><div class="grid-line-v grid-v2"></div>
            </div>
            <div class="focus-points" v-if="caps&&caps.focus">
              <div class="point-row"><text class="focus-tap" @click="focusAt(0.2,0.2)">·</text><text class="focus-tap" @click="focusAt(0.5,0.2)">·</text><text class="focus-tap" @click="focusAt(0.8,0.2)">·</text></div>
              <div class="point-row"><text class="focus-tap" @click="focusAt(0.2,0.5)">·</text><text class="focus-tap" @click="focusAt(0.5,0.5)">＋</text><text class="focus-tap" @click="focusAt(0.8,0.5)">·</text></div>
              <div class="point-row"><text class="focus-tap" @click="focusAt(0.2,0.8)">·</text><text class="focus-tap" @click="focusAt(0.5,0.8)">·</text><text class="focus-tap" @click="focusAt(0.8,0.8)">·</text></div>
            </div>
            <div v-if="countdown>0" class="countdown-overlay"><text class="countdown-number">{{countdown}}</text></div>
            <text v-else-if="busy&&mode==='photo'" class="busy-badge">连拍 {{shotsCompleted}}/{{burstCount}}</text>
          </div>
          <div class="toolbar">
            <text class="status">{{status}}</text>
            <scroller scroll-direction="horizontal" show-scrollbar="false" class="filters"><div class="filter-list"><text v-for="f in filters" :key="f.id" class="filter" :class="filter===f.id?'chosen':''" @click="chooseFilter(f.id)">{{f.label}}</text></div></scroller>
          </div>
        </div>
        <div v-if="panel==='gallery'" class="page">
          <div class="page-head"><text class="page-title">我的影像 · {{media.length}}</text><text class="nav-link" @click="refreshGallery">刷新</text><text class="nav-link" @click="panel='camera'">返回取景</text></div>
          <div class="filter-bar"><text class="filter" :class="mediaTypeFilter==='all'?'chosen':''" @click="setTypeFilter('all')">全部</text><text class="filter" :class="mediaTypeFilter==='photo'?'chosen':''" @click="setTypeFilter('photo')">照片</text><text class="filter" :class="mediaTypeFilter==='video'?'chosen':''" @click="setTypeFilter('video')">视频</text><text class="nav-link" @click="selectAllVisible">全选</text><text class="nav-link" @click="clearSelection">取消选择</text></div>
          <scroller class="list" scroll-direction="vertical" :show-scrollbar="true">
            <div v-if="!visibleMedia.length" class="empty"><text>当前没有可显示的影像</text></div>
            <div class="media-row" v-for="file in visibleMedia" :key="file.path" @click="toggle(file.path)"><text class="check">{{selected.indexOf(file.path)>=0?'☑':'□'}}</text><text class="filename">{{file.name||fileName(file.path)}}</text><text class="kind">{{file.type==='video'?'视频':'照片'}}</text><text class="bytes">{{size(file.size)}}</text></div>
          </scroller>
          <div class="page-foot"><text class="subtle">{{selected.length}} 项已选</text><text class="primary" @click="sharePanel">导出所选</text></div>
        </div>
        <div v-if="panel==='share'" class="page">
          <div class="page-head"><text class="page-title">局域网导出</text><text class="nav-link" @click="gallery">选择文件</text></div>
          <div class="share-layout"><div class="share-instructions"><text class="share-title">扫码或输入地址下载</text><text class="subtle">手机与 X7 Pro 连接同一 Wi-Fi</text><text class="url">{{shareUrl||'请先开启局域网分享'}}</text><text class="subtle">待分享 {{chosenFiles.length}} 项 · 退出后自动关闭</text><text v-if="!shareUrl" class="primary share-button" @click="share">开始分享</text><text v-else class="danger share-button" @click="stopShare">结束分享</text></div><image v-if="qr" class="qr" :src="qr" resize="contain"/><div v-else class="qr-empty"><text>QR</text></div></div>
        </div>
        <div v-if="panel==='settings'" class="page settings-page">
          <div class="page-head"><text class="page-title">拍摄设置</text><text class="nav-link" @click="panel='camera'">完成</text></div>
          <scroller class="settings-scroll" scroll-direction="vertical" :show-scrollbar="true">
            <div class="setting-row"><text class="setting-label">倒计时</text><div class="setting-options"><text v-for="t in timerChoices" :key="t" class="chip" :class="timerDelay===t?'chip-active':''" @click="setTimer(t)">{{t===0?'关闭':t+' 秒'}}</text></div></div>
            <div class="setting-row"><text class="setting-label">连续拍摄</text><div class="setting-options"><text v-for="n in burstChoices" :key="n" class="chip" :class="burstCount===n?'chip-active':''" @click="setBurst(n)">{{n}} 张</text></div></div>
            <div class="setting-row"><text class="setting-label">九宫格辅助线</text><text class="chip" :class="showGrid?'chip-active':''" @click="toggleGrid">{{showGrid?'开启':'关闭'}}</text></div>
            <div class="setting-row"><text class="setting-label">界面布局</text><text class="chip" @click="cycleLayout">{{layoutLabel}} · 点击切换</text></div>
            <text class="setting-hint">竖屏选项只改变界面排布，是否能旋转设备显示方向由 X7 Pro 固件决定。</text>
            <text class="setting-hint">照片/录像功能需真机适配原生摄像头。拍照期间切换后台会取消剩余的倒计时或连拍。</text>
          </scroller>
        </div>
      </div>
      <div v-if="panel==='camera'" class="controls">
        <div class="shutter-wrap"><div class="shutter" :class="recording?'shutter-on':''" @click="shutter"><div class="shutter-inner" :class="mode==='video'?'video-shutter':''"></div></div></div>
        <text class="shutter-caption">{{busy&&mode==='photo'?'点击取消':(recording?'结束录像':(mode==='photo'?'拍照':'开始录像'))}}</text>
        <div class="quick-settings" v-if="mode==='photo'"><text class="quick-key" @click="setTimer(timerChoices[(timerChoices.indexOf(timerDelay)+1)%timerChoices.length])">⏱ {{timerDelay===0?'关闭':timerDelay+'s'}}</text><text class="quick-key" @click="setBurst(burstChoices[(burstChoices.indexOf(burstCount)+1)%burstChoices.length])">▣ {{burstCount}} 张</text><text class="quick-key" @click="toggleGrid"># {{showGrid?'开':'关'}}</text></div>
        <div class="zoom"><text class="zoom-key" @click="zoomBy(-0.5)">−</text><text class="zoom-indicator">{{zoom.toFixed(1)}}×</text><text class="zoom-key" @click="zoomBy(0.5)">+</text></div>
      </div>
    </div>
    <div class="toast" v-if="toast"><text>{{toast}}</text></div>
  </div>
</template>
<script>
import index from './index.js'
export default index
</script>
<style lang="less" scoped>
@import url('../../styles/index.less');
</style>
