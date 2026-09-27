<template>
  <div class="login">
    <div class="login-shell">
      <section class="login-brand">
        <div class="brand-lockup">
          <img :src="riderguardLogo" alt="RiderGuard" class="brand-logo" />
          <span class="brand-pill">RIDERGUARD / SAFETY OPERATIONS</span>
        </div>
        <h1 class="brand-title">守护每一程<br /><em>让骑行更安全</em></h1>
        <p class="brand-desc">
          RiderGuard 将车载感知、实时定位与 AI 风险分析汇聚到一个安全运营中心，
          <br />让每一次配送都能被看见、被分析、被及时守护。
        </p>
        <div class="brand-footnote">骑手安全 · 实时感知 · 风险处置</div>
      </section>

      <el-form ref="loginRef" :model="loginForm" :rules="loginRules" class="login-form">
        <div class="title-box">
          <div>
            <p class="eyebrow">RIDERGUARD CONTROL CENTER</p>
            <h3 class="title">登录工作台</h3>
            <p class="subtitle">登录安全运营中心，查看骑手与设备状态。</p>
          </div>
          <lang-select />
        </div>

        <el-form-item prop="username">
          <el-input
            v-model="loginForm.username"
            type="text"
            size="large"
            auto-complete="off"
            :placeholder="$t('login.username')"
          >
            <template #prefix><svg-icon icon-class="user" class="el-input__icon input-icon" /></template>
          </el-input>
        </el-form-item>

        <el-form-item prop="password">
          <el-input
            v-model="loginForm.password"
            type="password"
            size="large"
            auto-complete="off"
            :placeholder="$t('login.password')"
            @keyup.enter="handleLogin"
          >
            <template #prefix><svg-icon icon-class="password" class="el-input__icon input-icon" /></template>
          </el-input>
        </el-form-item>

        <el-form-item v-if="captchaEnabled" prop="code" class="captcha-row">
          <el-input
            v-model="loginForm.code"
            size="large"
            auto-complete="off"
            :placeholder="$t('login.code')"
            @keyup.enter="handleLogin"
          >
            <template #prefix><svg-icon icon-class="validCode" class="el-input__icon input-icon" /></template>
          </el-input>
          <div class="login-code">
            <img v-if="codeUrl" :src="codeUrl" class="login-code-img" alt="验证码，点击刷新" @click="getCode" @error="handleCaptchaImageError" />
            <button v-else type="button" class="captcha-retry" :disabled="captchaLoading" @click="getCode">
              {{ captchaLoading ? '加载中' : '重试获取' }}
            </button>
          </div>
        </el-form-item>
        <p v-if="captchaError" class="captcha-error" role="alert">验证码服务暂不可用，请稍后重试或联系管理员。</p>

        <div class="form-meta">
          <el-checkbox v-model="loginForm.rememberMe">{{ $t('login.rememberPassword') }}</el-checkbox>
          <router-link v-if="register" class="link-type" :to="'/register'">
            {{ $t('login.switchRegisterPage') }}
          </router-link>
        </div>

        <el-form-item class="submit-row">
          <el-button :loading="loading" :disabled="captchaEnabled && !codeUrl" size="large" type="primary" class="submit-button" @click.prevent="handleLogin">
            <span v-if="!loading">{{ $t('login.login') }}</span>
            <span v-else>{{ $t('login.logging') }}</span>
          </el-button>
        </el-form-item>
      </el-form>
    </div>

    <div class="el-login-footer">
      <span>RiderGuard · 智能安全运营中心</span>
    </div>
  </div>
</template>

<script setup lang="ts">
import { to } from 'await-to-js';
import { useI18n } from 'vue-i18n';
import riderguardLogo from '@/assets/logo/riderguard-mark.svg';
import { getCodeImg } from '@/api/login';
import { LoginData } from '@/api/types';
import { useUserStore } from '@/store/modules/user';
import { hideEntrance, showEntrance } from '@/utils/entrance';

const userStore = useUserStore();
const router = useRouter();
const { t } = useI18n();

const loginForm = ref<LoginData>({
  username: 'admin',
  password: 'admin123',
  rememberMe: false,
  code: '',
  uuid: ''
} as LoginData);

const loginRules: ElFormRules = {
  username: [
    {
      required: true,
      trigger: 'blur',
      message: t('login.rule.username.required')
    }
  ],
  password: [
    {
      required: true,
      trigger: 'blur',
      message: t('login.rule.password.required')
    }
  ],
  code: [
    {
      required: true,
      trigger: 'change',
      message: t('login.rule.code.required')
    }
  ]
};

const codeUrl = ref('');
const loading = ref(false);
const captchaLoading = ref(false);
const captchaError = ref(false);
const captchaEnabled = ref(true);
const register = ref(false);
const redirect = ref('/');
const loginRef = ref<ElFormInstance>();

watch(
  () => router.currentRoute.value,
  (newRoute: any) => {
    redirect.value = newRoute.query && newRoute.query.redirect && decodeURIComponent(newRoute.query.redirect);
  },
  { immediate: true }
);

const handleLogin = () => {
  loginRef.value?.validate(async (valid: boolean, fields: any) => {
    if (valid) {
      loading.value = true;
      if (loginForm.value.rememberMe) {
        localStorage.setItem('username', String(loginForm.value.username));
        localStorage.setItem('rememberMe', String(loginForm.value.rememberMe));
      } else {
        localStorage.removeItem('username');
        localStorage.removeItem('rememberMe');
      }
      localStorage.removeItem('password');
      const [err] = await to(userStore.login(loginForm.value));
      if (!err) {
        const redirectUrl = redirect.value || '/';
        const shownAt = showEntrance();
        try {
          await router.push(redirectUrl);
        } finally {
          await hideEntrance(shownAt);
          loading.value = false;
        }
      } else {
        loading.value = false;
        if (captchaEnabled.value) {
          await getCode();
        }
      }
    } else {
      console.log('error submit!', fields);
    }
  });
};

const getCode = async () => {
  captchaLoading.value = true;
  captchaError.value = false;
  codeUrl.value = '';
  try {
    const res = await getCodeImg();
    const { data } = res;
    captchaEnabled.value = data.captchaEnabled === undefined ? true : data.captchaEnabled;
    if (captchaEnabled.value) {
      if (!data.img || !data.uuid) throw new Error('验证码数据不完整');
      loginForm.value.code = '';
      codeUrl.value = 'data:image/gif;base64,' + data.img;
      loginForm.value.uuid = data.uuid;
    }
  } catch {
    captchaEnabled.value = true;
    captchaError.value = true;
  } finally {
    captchaLoading.value = false;
  }
};

const handleCaptchaImageError = () => {
  codeUrl.value = '';
  captchaError.value = true;
};

const getLoginData = () => {
  const username = localStorage.getItem('username');
  const rememberMe = localStorage.getItem('rememberMe');
  localStorage.removeItem('password');
  loginForm.value = {
    username: username === null ? String(loginForm.value.username) : username,
    password: username === null ? String(loginForm.value.password) : '',
    rememberMe: rememberMe === 'true'
  } as LoginData;
};

onMounted(() => {
  getLoginData();
  getCode();
});
</script>

<style lang="scss" scoped>
.login {
  position: relative;
  display: flex;
  min-height: 100vh;
  min-height: 100svh;
  box-sizing: border-box;
  background: #fff;
}
.login-shell {
  display: grid;
  grid-template-columns: minmax(0, 52%) minmax(0, 48%);
  width: 100%;
  min-height: 100vh;
  min-height: 100svh;
  overflow: hidden;
  background: #fff;
}
.login-brand {
  display: flex;
  flex-direction: column;
  justify-content: space-between;
  min-height: 100vh;
  min-height: 100svh;
  padding: clamp(30px, 5vw, 76px);
  box-sizing: border-box;
  color: #fff;
  background: #171a20;
}
.brand-lockup { display: flex; align-items: center; gap: 12px; }
.brand-logo { display: block; width: 36px; height: 36px; }
.brand-pill { color: #b5bcc2; font-size: 10px; font-weight: 650; letter-spacing: .16em; }
.brand-title {
  margin: auto 0 18px;
  font-size: clamp(38px, 4.5vw, 58px);
  line-height: 1.09;
  letter-spacing: -.055em;
  font-weight: 650;
}
.brand-title em { color: #ef6b25; font-style: normal; }
.brand-desc { max-width: 430px; margin: 0; color: #a9b0b7; font-size: 13px; line-height: 1.9; }
.brand-footnote {
  margin-top: 55px;
  padding-top: 20px;
  border-top: 1px solid #353a40;
  color: #909aa2;
  font-size: 11px;
  letter-spacing: .08em;
}
.login-form {
  display: flex;
  flex-direction: column;
  justify-content: center;
  align-items: center;
  padding: 60px 8%;
  box-sizing: border-box;
  background: #fff;
}
.login-form > * { width: min(420px, 100%); }
.title-box { display: flex; align-items: flex-start; justify-content: space-between; gap: 12px; margin-bottom: 30px; }
.title-box .eyebrow { margin: 0 0 10px; color: #d6652c; font-size: 10px; font-weight: 700; letter-spacing: .16em; }
.title-box .title { margin: 0; color: #171a20; font-size: 29px; line-height: 1.2; letter-spacing: -.04em; }
.title-box .subtitle { margin: 11px 0 0; color: #8b949c; font-size: 12px; line-height: 1.7; }
.title-box :deep(.lang-select--style) { padding: 8px; border: 1px solid #e8ebea; border-radius: 8px; color: #737d85; }
.login-form .el-input { height: 46px; }
.login-form .input-icon { width: 14px; height: 44px; }
.login-form :deep(.el-input__wrapper) { min-height: 46px; border-radius: 8px; background: #fafbfa; box-shadow: 0 0 0 1px #e5e9e8 inset; }
.login-form :deep(.el-input__wrapper.is-focus) { box-shadow: 0 0 0 1px #ef6b25 inset, 0 0 0 3px rgba(239, 107, 37, .1); }
.captcha-row :deep(.el-form-item__content) { display: grid; grid-template-columns: minmax(0, 1fr) 122px; gap: 10px; }
.login-code { height: 46px; overflow: hidden; border: 1px solid #e5e9e8; border-radius: 8px; background: #fafbfa; }
.login-code img { display: block; width: 100%; height: 100%; object-fit: cover; cursor: pointer; }
.captcha-retry { width: 100%; height: 100%; border: 0; background: #f5f7f6; color: #bd5b27; font-size: 11px; cursor: pointer; }
.captcha-retry:disabled { color: #98a1a8; cursor: wait; }
.captcha-error { margin: -12px 0 15px; color: #b75434; font-size: 11px; line-height: 1.5; }
.form-meta { display: flex; align-items: center; justify-content: space-between; gap: 12px; margin: 0 0 18px; }
.login-form :deep(.el-checkbox__label) { color: #7c858d; }
.submit-row { margin-bottom: 0; }
.submit-button { width: 100%; height: 47px; border-radius: 8px; font-weight: 650; }
.el-login-footer { position: absolute; right: 0; bottom: 22px; width: 48%; color: #a4acb1; font-size: 11px; text-align: center; }
@media (max-width: 850px) {
  .login-shell { grid-template-columns: 1fr; }
  .login-brand { min-height: 210px; padding: 25px 28px; }
  .brand-title { margin: 24px 0 10px; font-size: 31px; }
  .brand-desc, .brand-footnote { display: none; }
  .login-form { padding: 30px 28px; }
  .el-login-footer { width: 100%; }
}
@media (max-width: 480px) {
  .login { padding-bottom: 55px; }
  .login-brand { min-height: 165px; }
  .brand-title { font-size: 26px; }
  .login-form { padding: 24px 18px; }
  .title-box { margin-bottom: 22px; }
  .captcha-row :deep(.el-form-item__content) { grid-template-columns: 1fr; }
}
</style>
