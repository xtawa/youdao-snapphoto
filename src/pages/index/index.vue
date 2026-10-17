<template>
  <div class="screen">
    <div class="header">
      <div class="brand"><text class="brand-name">SnapPhoto</text><text class="brand-sub">X7 PRO CAMERA</text></div>
      <div class="head-actions"><text class="state">{{recording?'● REC '+elapsed:(connected?'● CAMERA READY':'● OFFLINE')}}</text><text class="head-btn" @click="connect">重连</text><text class="head-btn" @click="exit">退出</text></div>
    </div>
    <div class="layout">
      <div class="sidebar">
        <text class="mini-label">CAPTURE</text>
        <div class="menu-item" :class="panel==='camera'&&mode==='photo'?'active':''" @click="changeMode('photo')"><text class="menu-icon">◎</text><text class="menu-text">拍照</text></div>
        <div class="menu-item" :class="panel==='camera'&&mode==='video'?'active':''" @click="changeMode('video')"><text class="menu-icon">▣</text><text class="menu-text">录像</text></div>
        <div class="menu-item" :class="panel==='gallery'?'active':''" @click="gallery"><text class="menu-icon">▦</text><text class="menu-text">相册</text></div>
        <div class="menu-item" :class="panel==='share'?'active':''" @click="sharePanel"><text class="menu-icon">⇧</text><text class="menu-text">导出</text></div>
        <text class="counter">{{media.length}} ITEMS</text>
      </div>
      <div class="main">
        <div v-if="panel==='camera'" class="camera">
          <div class="view">
            <image v-if="preview" class="preview" :src="preview" resize="contain"/>
            <div v-else class="placeholder"><text class="large-icon">◉</text><text class="placeholder-heading">{{connected?'等待相机取景':'摄像头未连接'}}</text><text class="placeholder-caption">{{connected?'等待原生取景帧':'需适配 X7 Pro 原生摄像头接口'}}</text></div>
            <text class="view-badge">{{mode==='photo'?'PHOTO':'VIDEO'}} · {{zoom.toFixed(1)}}×</text>
            <div class="focus-points"><div class="point-row"><text @click="focusAt(0.2,0.2)">·</text><text @click="focusAt(0.5,0.2)">·</text><text @click="focusAt(0.8,0.2)">·</text></div><div class="point-row"><text @click="focusAt(0.2,0.5)">·</text><text @click="focusAt(0.5,0.5)">＋</text><text @click="focusAt(0.8,0.5)">·</text></div><div class="point-row"><text @click="focusAt(0.2,0.8)">·</text><text @click="focusAt(0.5,0.8)">·</text><text @click="focusAt(0.8,0.8)">·</text></div></div>
          </div>
          <div class="toolbar"><text class="status">{{status}}</text><div class="filters"><text v-for="f in filters" :key="f.id" class="filter" :class="filter===f.id?'chosen':''" @click="chooseFilter(f.id)">{{f.label}}</text></div></div>
        </div>
        <div v-if="panel==='gallery'" class="page"><div class="page-head"><text class="page-title">我的影像</text><text class="nav-link" @click="refreshGallery">刷新</text><text class="nav-link" @click="panel='camera'">取景</text></div><scroller class="list" scroll-direction="vertical" :show-scrollbar="true"><div v-if="!media.length" class="empty"><text>还没有照片或视频</text></div><div class="media-row" v-for="file in media" :key="file.path" @click="toggle(file.path)"><text class="check">{{selected.indexOf(file.path)>=0?'☑':'□'}}</text><text class="filename">{{file.name||fileName(file.path)}}</text><text class="kind">{{file.type==='video'?'视频':'照片'}}</text><text class="bytes">{{size(file.size)}}</text></div></scroller><div class="page-foot"><text class="subtle">{{selected.length}} 项已选</text><text class="primary" @click="sharePanel">导出所选</text></div></div>
        <div v-if="panel==='share'" class="page"><div class="page-head"><text class="page-title">局域网导出</text><text class="nav-link" @click="gallery">选择文件</text><text class="nav-link" @click="panel='camera'">取景</text></div><div class="share-layout"><div class="share-instructions"><text class="share-title">扫码或输入地址下载</text><text class="subtle">接收设备与 X7 Pro 连接同一个 Wi-Fi</text><text class="url">{{shareUrl||'请先开启局域网分享'}}</text><text class="subtle">待分享 {{selected.length||media.length}} 项 · 离开应用时自动关闭</text><text v-if="!shareUrl" class="primary share-button" @click="share">开始分享</text><text v-else class="danger share-button" @click="stopShare">结束分享</text></div><image v-if="qr" class="qr" :src="qr" resize="contain"/><div v-else class="qr-empty"><text>QR</text></div></div></div>
      </div>
      <div v-if="panel==='camera'" class="controls"><text class="mini-label">MODE</text><text class="mode-title">{{mode==='photo'?'PHOTO':'VIDEO'}}</text><div class="shutter-wrap"><div class="shutter" :class="recording?'shutter-on':''" @click="shutter"><div class="shutter-inner" :class="mode==='video'?'video-shutter':''"></div></div></div><text class="shutter-caption">{{recording?'结束录像':(mode==='photo'?'点击拍摄':'开始录像')}}</text><div class="zoom"><text class="zoom-key" @click="zoomBy(-0.5)">−</text><text class="zoom-indicator">{{zoom.toFixed(1)}}×</text><text class="zoom-key" @click="zoomBy(0.5)">+</text></div><text class="control-caption">点选取景区域对焦</text></div>
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
